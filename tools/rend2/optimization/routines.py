"""Bounded numerical method adapters, explicit work/status reporting.

SciPy provides reference NNLS/BVLS/TRF/L-BFGS-B/Powell. Small projected CD,
group-prox, damped GN and Adam loops are benchmark implementations, not a
production numerical library. SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass, asdict
import math
import time
from environment import np, optimize


class WorkLimit(RuntimeError): pass
class Cancelled(RuntimeError): pass
class Infeasible(RuntimeError): pass


@dataclass
class Work:
    forward: int = 0
    derivative: int = 0
    scalar_responses: int = 0
    rays: int = 0
    visibility_tests: int = 0
    projections: int = 0
    matvec: int = 0


def checked(value):
    a = np.asarray(value, dtype=float)
    if not np.all(np.isfinite(a)): raise ValueError('Nonfinite numerical input')
    return a


def interval_residual(value, lo, hi):
    value, lo = checked(value), checked(lo); hi = np.asarray(hi, dtype=float)
    if value.shape != lo.shape or hi.shape != lo.shape or np.any(np.isnan(hi)) or np.any(hi < lo):
        raise ValueError('Observation interval shape/range')
    return np.where(value < lo, value-lo, np.where(value > hi, value-hi, 0.))


class Objective:
    def __init__(self, model, lower, upper, x0, lo, hi, weights=None, scale=1., budget=800,
                 jacobian=None, feasible=None, loss='linear', loss_scale=1., cancelled=None):
        self.lower, self.upper, self.x0 = map(checked, (lower, upper, x0))
        if self.lower.ndim != 1 or self.upper.shape != self.lower.shape or self.x0.shape != self.lower.shape:
            raise ValueError('Parameter span')
        if np.any(self.lower >= self.upper) or np.any(self.x0 < self.lower) or np.any(self.x0 > self.upper):
            raise Infeasible('Initial/bounds infeasible')
        self.lo, self.hi = checked(lo), np.asarray(hi, dtype=float)
        self.weights = np.ones_like(self.lo) if weights is None else checked(weights)
        if (self.lo.shape != self.hi.shape or self.weights.shape != self.lo.shape or
            np.any(self.weights < 0) or not self.weights.sum() > 0 or np.any(np.isnan(self.hi)) or np.any(self.hi < self.lo)):
            raise ValueError('Invalid targets/weights')
        if not math.isfinite(scale) or scale <= 0 or not 1 <= budget <= 100000: raise ValueError('Scale/work budget')
        self.factor = np.sqrt(self.weights/self.weights.sum())/scale
        self.model, self.model_jacobian, self.feasible = model, jacobian, feasible
        self.loss, self.loss_scale = loss, loss_scale
        self.work, self.budget, self.cancelled = Work(), budget, cancelled
        self.best = None; self.last_x = None; self.last_r = None

    def residual(self, x):
        x = checked(x)
        if self.cancelled and self.cancelled(): raise Cancelled('Injected cancellation')
        if self.work.forward >= self.budget: raise WorkLimit('Forward evaluation budget exhausted')
        if np.any(x < self.lower-1e-9) or np.any(x > self.upper+1e-9): raise Infeasible('Proposal outside box')
        self.work.forward += 1
        if self.work.scalar_responses+self.work.rays+self.work.projections > 200000:
            raise WorkLimit('Response/ray/projection work budget exhausted (200000)')
        if self.feasible and not self.feasible(x):
            # A finite rejection residual is an optimization barrier, NOT fabricated light.
            return np.full(self.lo.size, 1e3+np.linalg.norm(x-self.x0))
        prediction = checked(self.model(x, self.work))
        r = (interval_residual(prediction, self.lo, self.hi)*self.factor).ravel()
        f, _ = self.loss_terms(r)
        if self.best is None or f < self.best[0]: self.best = (f, x.copy())
        self.last_x, self.last_r = x.copy(), r.copy()
        return r

    def loss_terms(self, r):
        z = r/self.loss_scale
        if self.loss == 'linear': rho, psi = .5*r*r, r
        elif self.loss == 'huber':
            rho = self.loss_scale**2*np.where(abs(z) <= 1, .5*z*z, abs(z)-.5)
            psi = self.loss_scale*np.clip(z, -1, 1)
        elif self.loss == 'cauchy':
            rho = .5*self.loss_scale**2*np.log1p(z*z); psi = r/(1+z*z)
        else: raise ValueError('Unspecified robust loss')
        return float(rho.sum()), psi

    def jac(self, x, step=1e-5):
        self.work.derivative += 1; x = checked(x)
        if self.model_jacobian:
            prediction, J = self.model_jacobian(x, self.work)
            active = ((self.lo == self.hi) | (prediction < self.lo) | (prediction > self.hi)).ravel()
            return np.asarray(J)*self.factor.ravel()[:, None]*active[:, None]
        columns = []
        for k in range(len(x)):
            h = step*(self.upper[k]-self.lower[k]); a, b = x.copy(), x.copy()
            a[k] = min(self.upper[k], x[k]+h); b[k] = max(self.lower[k], x[k]-h)
            columns.append((self.residual(a)-self.residual(b))/(a[k]-b[k]))
        return np.column_stack(columns)

    def fg(self, x):
        r = self.residual(x); f, psi = self.loss_terms(r)
        return f, self.jac(x).T@psi


def solve_objective(obj, method, iterations=120, rate=.025, warm=None):
    start = time.perf_counter(); x = obj.x0.copy() if warm is None else checked(warm).copy()
    status, message, nit, library_nfev = 'Stationary', '', 0, None
    try:
        if method == 'trf':
            result = optimize.least_squares(obj.residual, x, jac=obj.jac, bounds=(obj.lower, obj.upper),
                method='trf', ftol=1e-10, xtol=1e-9, gtol=1e-8, max_nfev=obj.budget,
                loss=obj.loss, f_scale=obj.loss_scale, x_scale=1.)
            x, nit, message = result.x, None, result.message
            library_nfev = result.nfev
            status = 'Stationary' if result.success else 'Budget'
        elif method == 'lbfgsb':
            result = optimize.minimize(obj.fg, x, jac=True, method='L-BFGS-B', bounds=list(zip(obj.lower, obj.upper)),
                options=dict(maxiter=iterations, maxfun=obj.budget, ftol=1e-12, gtol=1e-8, maxls=20))
            x, nit, message = result.x, result.nit, result.message
            status = 'Stationary' if result.success else 'StalledOrBudget'
        elif method == 'powell':
            result = optimize.minimize(lambda p: obj.loss_terms(obj.residual(p))[0], x, method='Powell',
                bounds=list(zip(obj.lower, obj.upper)), options=dict(maxiter=iterations, maxfev=obj.budget, xtol=1e-7, ftol=1e-10))
            x, nit, message = result.x, result.nit, result.message
            status = 'Stationary' if result.success else 'Budget'
        elif method in ('adam', 'projected_lm'):
            momentum = np.zeros_like(x); variance = momentum.copy(); damping = 1e-3; trust = .25
            for k in range(iterations):
                r = obj.residual(x); old, psi = obj.loss_terms(r); J = obj.jac(x); g = J.T@psi
                projected = x-np.clip(x-g, obj.lower, obj.upper)
                if np.max(abs(projected)) < 1e-8: status = 'Stationary'; break
                if method == 'adam':
                    momentum = .9*momentum+.1*g; variance = .999*variance+.001*g*g
                    proposed = np.clip(x-rate*(momentum/(1-.9**(k+1)))/(np.sqrt(variance/(1-.999**(k+1)))+1e-8), obj.lower, obj.upper)
                    change = np.max(abs(proposed-x)); x = proposed
                else:
                    H = J.T@J
                    try: step = np.linalg.solve(H+damping*np.eye(len(x)), -g)
                    except np.linalg.LinAlgError: damping *= 10; continue
                    if np.linalg.norm(step) > trust: step *= trust/np.linalg.norm(step)
                    proposed = np.clip(x+step, obj.lower, obj.upper)
                    new = obj.loss_terms(obj.residual(proposed))[0]; change = np.max(abs(proposed-x))
                    if new < old:
                        x = proposed; damping = max(1e-10, damping/3); trust = min(.5, trust*1.5)
                    else: damping = min(1e10, damping*10); trust *= .5
                nit = k+1
                if change < 1e-10: status = 'Stagnated'; break
            else: status = 'IterationLimit'
        else: raise ValueError('Unknown method')
    except (WorkLimit, Cancelled, Infeasible) as e:
        status, message = type(e).__name__, str(e)
    except (ValueError, FloatingPointError, np.linalg.LinAlgError) as e:
        status, message = 'NumericalFailure', str(e)
    if obj.best is not None: x = obj.best[1]
    return dict(x=x.tolist(), objective=obj.best[0] if obj.best else None, status=status,
                message=str(message), iterations=nit, library_nfev=library_nfev,
                work=asdict(obj.work), wall_seconds=time.perf_counter()-start,
                success_claim='Numerical stationarity/partial result only; not physical source confidence')


def linear_fit(A, Y, method, penalty=0., ridge=0., passes=500, x0=None, budget=10000):
    A, Y = checked(A), checked(Y)
    if A.ndim != 2 or Y.ndim != 2 or A.shape[0] != Y.shape[0] or Y.shape[1] != 3 or A.shape[1] > 64:
        raise ValueError('Linear RGB span/budget')
    if min(A.shape) == 0 or np.any(A < 0) or np.any(Y < 0) or penalty < 0 or ridge < 0: raise ValueError('Nonnegative fit')
    start = time.perf_counter(); n = A.shape[1]; X = np.zeros((n, 3)) if x0 is None else checked(x0).copy()
    work = Work(); iterations = 0; status = 'Stationary'
    s = np.linalg.svd(A, compute_uv=False); rank = int(sum(s > max(s[0]*1e-10, 1e-14)))
    try:
        if method in ('nnls', 'bvls', 'trf_linear'):
            if method == 'nnls': iterations = None
            for c in range(3):
                if method == 'nnls': X[:, c] = optimize.nnls(A, Y[:, c], maxiter=100*n)[0]
                else:
                    r = optimize.lsq_linear(A, Y[:, c], bounds=(0., np.inf),
                        method='bvls' if method == 'bvls' else 'trf', tol=1e-11, max_iter=passes)
                    X[:, c] = r.x; iterations += r.nit
                    if not r.success: status = 'IterationLimit'
            # Dense library-internal BLAS calls are opaque; do not invent their count.
            work.matvec = None
        elif method == 'coordinate':
            P = A@X; work.matvec += 3
            for iteration in range(passes):
                old = X.copy()
                for j in range(n):
                    col = A[:, j]; d = col@col+ridge
                    if d <= 1e-20: continue
                    v = np.maximum(0., (col@(Y-P+col[:, None]*X[j])-penalty)/d)
                    P += col[:, None]*(v-X[j]); X[j] = v; work.matvec += 3
                    if work.matvec > budget: raise WorkLimit('Matrix-vector budget')
                iterations = iteration+1
                if np.max(abs(X-old)) < 1e-10: break
            else: status = 'IterationLimit'
        elif method == 'group_prox':
            # RGB group L2,1 penalty on normalized response columns, common scaling.
            L = max(1e-20, s[0]**2+ridge); Z = X.copy(); t = 1.
            for iteration in range(passes):
                old = X.copy(); P = Z-(A.T@(A@Z-Y)+ridge*Z)/L; work.matvec += 6
                P = np.maximum(0., P); lengths = np.linalg.norm(P, axis=1)
                X = P*np.maximum(0., 1-penalty/(L*np.maximum(lengths, 1e-30)))[:, None]
                new_t = (1+math.sqrt(1+4*t*t))/2; Z = X+(t-1)/new_t*(X-old); t = new_t
                iterations = iteration+1
                if work.matvec > budget: raise WorkLimit('Matrix-vector budget')
                if np.max(abs(X-old)) < 1e-10: break
            else: status = 'IterationLimit'
        else: raise ValueError('Unknown linear method')
    except (RuntimeError, WorkLimit) as e: status = type(e).__name__
    gradient = A.T@(A@X-Y)+ridge*X
    if penalty == 0:
        kkt = float(np.max(abs(np.where(X > 1e-8, gradient, np.minimum(gradient, 0.)))))
    else: kkt = None
    return dict(x=X.tolist(), objective=float(.5*np.sum((A@X-Y)**2)), status=status, iterations=iterations,
        work=asdict(work), wall_seconds=time.perf_counter()-start, kkt_violation=kkt,
        rank=rank, columns=n, singular_values=s.tolist(), condition=float(s[0]/s[-1]) if rank == n else None,
        identification='RankDeficient' if rank < n else 'LocalFullRank',
        regularization=dict(group_or_coordinate_penalty=penalty, ridge=ridge))
