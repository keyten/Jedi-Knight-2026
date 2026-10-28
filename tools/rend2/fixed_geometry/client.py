"""A strict line-protocol client. All candidate predictions come from C++."""
import json
import math
import subprocess
import time

class ForwardUnavailable(RuntimeError):pass
class BudgetExceeded(RuntimeError):pass
class FitCancelled(RuntimeError):pass

class Client:
    def __init__(self,exe,request,budget=1800):
        self.process=subprocess.Popen([str(exe),'--server',str(request)],stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
        try:
            self.hello=self._read();self.budget=budget
            if self.hello.get('type')!='ready':raise ForwardUnavailable('No C++ preparation handshake')
        except Exception:
            if self.process.poll() is None:self.process.kill()
            self.process.communicate(timeout=15)
            raise
        self.work=dict(forward_calls=0,source_responses=0,rays=self.hello['preparation_rays'],
                       nodes=self.hello['preparation_nodes'],triangles=self.hello['preparation_triangles'],cache_hits=0)
        self.seconds=0.;self.wall=0.

    def _read(self):
        line=self.process.stdout.readline(1024*1024)
        if not line:raise ForwardUnavailable('C++ process closed: '+self.process.stderr.read(4096))
        if len(line)>=1024*1024:raise ForwardUnavailable('Response byte budget')
        return json.loads(line)

    def predict(self,x,command='E'):
        if self.work['forward_calls']>=self.budget:raise BudgetExceeded('Forward-call budget exhausted')
        if any(not math.isfinite(v) or v<0 for v in x):raise ValueError('Finite nonnegative amplitudes required')
        start=time.perf_counter();self.process.stdin.write(command+' '+' '.join(format(v,'.17g') for v in x)+'\n');self.process.stdin.flush()
        r=self._read();self.work['forward_calls']+=1;self.wall+=time.perf_counter()-start
        for key,name in [('pairs','source_responses'),('rays','rays'),('nodes','nodes'),('triangles','triangles'),('cache_hits','cache_hits')]:self.work[name]+=r[key]
        self.seconds+=r['seconds']
        if r['code']==6:raise FitCancelled('Production evaluator cancellation')
        if r['code']==8:raise BudgetExceeded('Production evaluator work budget')
        if r['code'] or not r['available'] or r['rgb'] is None:raise ForwardUnavailable('Unsupported/unknown C++ result '+str({k:r[k] for k in ('code','available','unavailable','status')}))
        if len(r['rgb'])!=self.hello['rows']:raise ForwardUnavailable('Truncated C++ predictions')
        return r

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.write('quit\n');self.process.stdin.flush()
        self.process.communicate(timeout=15)
        if self.process.returncode:raise ForwardUnavailable('C++ adapter failed')

    def __enter__(self):return self
    def __exit__(self,*args):self.close()
