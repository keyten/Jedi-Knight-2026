"""Feature-scoped bake hypotheses and a restricted CPU research evaluator.

No renderer defaults or physical calibration are inferred. Compiler scalar units
are NOT candela/lux. Unknown rules produce no fabricated RGB.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass, asdict, replace
import hashlib
import json
import math
from pathlib import Path
from bake_model import point_scalar, spot_factor, polygon_factor, dot, sub, unit

HERE = Path(__file__).resolve().parent
VERSION = 1
FEATURES = ('attenuation', 'angle', 'spot', 'point_scale', 'surface_integration',
            'sun_sky', 'grid_reduction', 'deluxe', 'bounce_fill', 'output',
            'sampling_visibility', 'absolute_calibration')
STATES = ('Unknown', 'Assumed', 'SourceSupported', 'VerifiedSubset')


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@dataclass(frozen=True)
class Rule:
    feature: str
    selector: str = ''
    knowledge: str = 'Unknown'
    parameters: tuple = ()
    provenance: tuple = ()
    scope: tuple = ()
    unsupported: tuple = ()

    def values(self):
        return dict(self.parameters)


@dataclass(frozen=True)
class NuisanceRange:
    name: str
    lower: float
    upper: float
    unit: str
    provenance: str


@dataclass(frozen=True)
class Hypothesis:
    stable_id: str
    family: str
    rules: tuple
    nuisance: tuple
    formula_version: int = VERSION
    catalogue_sha256: str = ''
    source_lock_sha256: str = ''

    def rule(self, name):
        return next(r for r in self.rules if r.feature == name)

    def digest(self):
        return hashlib.sha256(json.dumps(asdict(self), sort_keys=True,
            separators=(',', ':'), allow_nan=False).encode()).hexdigest()

    def validate(self):
        if (self.formula_version != VERSION or not self.stable_id or
                sorted(r.feature for r in self.rules) != sorted(FEATURES)):
            raise ValueError('Invalid/unsupported hypothesis schema')
        for r in self.rules:
            if r.knowledge not in STATES or (r.knowledge != 'Unknown' and
                    (not r.selector or not r.provenance)):
                raise ValueError('Missing feature provenance')
            if r.knowledge == 'VerifiedSubset' and not r.scope:
                raise ValueError('Verification requires a feature scope')
            for _, value in r.parameters:
                if isinstance(value, float) and not math.isfinite(value):
                    raise ValueError('Nonfinite profile parameter')
        for n in self.nuisance:
            if not math.isfinite(n.lower+n.upper) or n.lower > n.upper or not n.unit:
                raise ValueError('Invalid nuisance bounds')


class UnspecifiedModel(ValueError):
    pass


def catalogue():
    """Resolve the existing five-family catalogue; never assign a stock default."""
    original = json.loads((HERE/'profiles.json').read_text())
    entries={p['id']:p for p in original['profiles']}
    native=entries['NetRadiant-d9338309-JA']['source_supported_defaults']
    validation = json.loads((HERE/'validation.json').read_text())
    cat_hash, lock_hash = file_hash(HERE/'profiles.json'), file_hash(HERE/'source-lock.json')
    evidence = ('profiles.json:'+cat_hash, 'source-lock.json:'+lock_hash)
    verified = evidence+('validation.json:'+file_hash(HERE/'validation.json'),)
    ranges = (NuisanceRange('source_rgb_amplitude',0,10000,'compiler_relative',
                           'explicit experiment bound; not physical photometry'),
              NuisanceRange('geometry_offset',-128,128,'world_units','explicit bounded experiment prior'),
              NuisanceRange('gamma',1,2.2,'dimensionless','source-supported option'),
              NuisanceRange('compensate',1,2,'dimensionless','source-supported option'),
              NuisanceRange('compiler_exposure',0,255,'compiler_relative',
                            'ColorToBytes parameter, NOT exposure stops'),
              NuisanceRange('surface_fill',0,32,'compiler_relative','bounded discrepancy'))

    def make(identifier, family, law='', angle='cosine', knowledge='VerifiedSubset', spot=False):
        rules = {f: Rule(f) for f in FEATURES}
        if law:
            proof = verified if knowledge == 'VerifiedSubset' else evidence
            scopes = ('inverse_square','no_angle','linear','linear_angle','linear_fade','angle_scale',
                      'wolf_linear','wolf_inverse_square','pointscale_half','extra_distance','half_lambert','spot') if knowledge == 'VerifiedSubset' else ()
            def set_rule(f,s,**params):
                rules[f] = Rule(f,s,knowledge,tuple(sorted(params.items())),proof,scopes)
            defaults=entries.get(family,{}).get('source_supported_defaults',native)
            set_rule('attenuation',law,minimum_distance_world=float(defaults['minimum_point_distance_world']),
                     extra_distance_world=float(defaults.get('extra_distance_world',0)),fade=1.,fast=True,
                     tolerance=float(defaults['falloff_tolerance']))
            set_rule('angle',angle,angle_scale=.5 if angle=='scaled_cosine' else 0.)
            set_rule('point_scale','q3map_photons',point_scale=float(defaults['point_scale']),
                     linear_divisor=1/float(defaults['linear_scale']))
            set_rule('spot','q3map_radius16_edge32' if spot else 'not_a_spot',target_distance=128.,radius=64.)
            set_rule('output','q3map_ColorToBytes',gamma=1.,compensate=1.,exposure=0.,srgb=False)
            if knowledge=='VerifiedSubset':
                attenuation_scope=('linear','linear_angle','linear_fade','wolf_linear') if law=='linear' else ('inverse_square','no_angle','wolf_inverse_square','pointscale_half','extra_distance','spot')
                angle_scope={'half_lambert':('half_lambert',),'scaled_cosine':('angle_scale',),
                             'none':('no_angle','linear','linear_fade','wolf_linear'),
                             'cosine':('inverse_square','linear_angle','spot','wolf_inverse_square')}
                rules['attenuation']=replace(rules['attenuation'],scope=attenuation_scope)
                rules['angle']=replace(rules['angle'],scope=angle_scope[angle])
                rules['spot']=replace(rules['spot'],scope=('spot',) if spot else ('inverse_square','linear'))
                rules['output']=replace(rules['output'],scope=('gamma1/compensate1/exposure0 controlled fixtures',
                    'colour_gamma additionally verifies the g=2.2 recipe, not arbitrary gamma values'))
            # Only these PARTICULAR contributions are verified. Full transport,
            # tracing, arbitrary area geometry and RGB compression are not replayed.
            rules['surface_integration'] = Rule('surface_integration','signed_polygon_subset',
                knowledge,(),proof,('area: unoccluded floor, specified winding/push/subdivision',))
            rules['sun_sky'] = Rule('sun_sky','specified_sun_and_17_direction_sky',knowledge,(),proof,
                ('sun','multi_sun','skylight'),('general sky RNG/sample count',))
            rules['grid_reduction'] = Rule('grid_reduction','single_pass_weighted_direction',
                knowledge,(('directed_leak',0.),('merge_epsilon',4)),proof,
                ('single-source interior; supplied contributions',),('ordered writer replay','bounce recurrence'))
            rules['deluxe'] = Rule('deluxe','weighted_world_direction_with_normal_bias',knowledge,(),proof,
                ('specified unfiltered floor fixtures',),('general/tangent-basis replay',))
        if family == 'Q3MapOriginal':
            # Do not inherit JA deluxe/area/sky claims from the modern compiler.
            for feature in ('surface_integration','sun_sky','deluxe'):
                rules[feature]=Rule(feature,unsupported=('not evaluated by this historical direct candidate',))
            rules['attenuation'] = replace(rules['attenuation'],parameters=tuple(sorted(dict(
                rules['attenuation'].values(),fast=False).items())))
            rules['grid_reduction'] = Rule('grid_reduction','q3_single_pass', 'SourceSupported',
                (('directed_leak',.25),),evidence,(),('grid spot cone absent','no compiler execution'))
            rules['output']=replace(rules['output'],knowledge='Assumed',scope=(),
                provenance=evidence+('explicit gamma1/compensate1/exposure0 completion for direct comparison only',))
        if family == 'Q3Map2-2.5.16-JA':
            rules['deluxe']=Rule('deluxe',unsupported=('historical direction replay not specified here',))
            rules['grid_reduction'] = Rule('grid_reduction','gtk2516_single_pass','SourceSupported',
                (('directed_leak',.125),('merge_epsilon',4)),evidence,(),('bounce recurrence not replayed',))
            rules['sun_sky'] = Rule('sun_sky','shader_sun_euclidean_rgb','SourceSupported',(),evidence,(),
                ('exact historical sky sampler execution',))
        rules['bounce_fill'] = Rule('bounce_fill','bounded_unexplained_fill','Assumed',(),
            ('experiment discrepancy, NOT GI/transport replay',),(),('full bounce algorithm',)) if law else rules['bounce_fill']
        h=Hypothesis(identifier,family,tuple(rules[f] for f in FEATURES),ranges,VERSION,cat_hash,lock_hash)
        h.validate(); return h

    # These choices are named options/countermodels in the existing research,
    # not a sweep of invented compiler identities.
    output=[make('UnknownProfile','UnknownProfile'),make('RavenProductionUnknown','RavenProductionUnknown')]
    for suffix,law,angle,spot in [('isq_cos','inverse_square','cosine',False),
        ('linear_noangle','linear','none',False),('linear_cos','linear','cosine',False),
        ('isq_noangle','inverse_square','none',False),('isq_half_lambert','inverse_square','half_lambert',False),
        ('isq_scaled_angle','inverse_square','scaled_cosine',False),('spot','inverse_square','cosine',True)]:
        output.append(make('NetRadiant-d9338309-JA/'+suffix,'NetRadiant-d9338309-JA',law,angle,spot=spot))
    output += [make('Q3MapOriginal/direct-isq','Q3MapOriginal','inverse_square',knowledge='SourceSupported'),
               make('Q3Map2-2.5.16-JA/direct-isq','Q3Map2-2.5.16-JA','inverse_square',knowledge='SourceSupported'),
               make('Assumed/isq16-cos','UnattributedAssumption','inverse_square',knowledge='Assumed'),
               make('Assumed/linear16-noangle','UnattributedAssumption','linear','none',knowledge='Assumed')]
    assert {p['id'] for p in original['profiles']} <= {h.family for h in output}
    return {h.stable_id:h for h in output}


def continuous_encode(rgb, gamma=1., compensate=1., exposure=0., srgb=False):
    """Existing ColorToBytes before byte quantization; not source calibration.

    Shared-peak normalization happens BEFORE comparing channel intervals.
    Tests compare its quantization against bake_model.encode_color.
    """
    if not all(math.isfinite(x) and x>=0 for x in rgb) or not math.isfinite(gamma+compensate+exposure) or gamma<=0 or compensate<=0 or exposure<0:
        raise ValueError('Invalid encoding input')
    values=[255*(x/255)**(1/gamma) for x in rgb]
    peak=max(values)
    gain=(255/peak if peak>255 else 1.) if exposure==0 else (255*(-math.expm1(-peak/exposure))/peak if peak else 0.)
    values=[x*gain/compensate for x in values]
    if srgb:
        values=[255*(12.92*(x/255) if x/255<=.0031308 else 1.055*(x/255)**(1/2.4)-.055) for x in values]
    if any(not math.isfinite(x) or x<0 or x>255.00001 for x in values):
        raise ValueError('Unsupported byte-cast domain')
    return values


def require(hypothesis, *features):
    hypothesis.validate()
    for feature in features:
        rule=hypothesis.rule(feature)
        if rule.knowledge=='Unknown' or not rule.selector:
            raise UnspecifiedModel(feature)
    if 'attenuation' in features:
        r=hypothesis.rule('attenuation')
        if r.selector not in ('inverse_square','linear') or r.values().get('minimum_distance_world')!=16.:
            raise UnspecifiedModel('unsupported attenuation selector/clamp')
    if 'angle' in features and hypothesis.rule('angle').selector not in ('cosine','none','half_lambert','scaled_cosine'):
        raise UnspecifiedModel('unsupported angular rule')
    if 'spot' in features and hypothesis.rule('spot').selector not in ('not_a_spot','q3map_radius16_edge32'):
        raise UnspecifiedModel('unsupported spot rule')
    if 'output' in features and hypothesis.rule('output').selector!='q3map_ColorToBytes':
        raise UnspecifiedModel('unsupported output encoding')


def evaluate(hypothesis, position, normal, sources, visibility, encoding=None, fill=(0.,0.,0.)):
    """Restricted unfiltered surface-direct request; all inputs finite world space.

    sources: tuples (origin, RGB amplitudes Q, spot propagation axis).
    I=max(Q), c=Q/I enforces the compiler colour gauge. In particular linear
    attenuation is c*max(A*I*7500/8000-d,0), NOT gain*(fixedI-d).
    visibility must be explicitly supplied per source; None means Unknown.
    Missing native trace/filter/bounce rules remain model discrepancy, never 0.
    """
    require(hypothesis,'attenuation','angle','point_scale','spot','output')
    if len(sources)!=len(visibility) or any(v not in (True,False,None) for v in visibility):
        raise ValueError('Visibility/source mismatch')
    if any(v is None for v in visibility): raise UnspecifiedModel('visibility for this segment')
    if len(position)!=3 or len(normal)!=3 or len(fill)!=3 or not all(math.isfinite(v) for v in (*position,*normal,*fill)) or any(v<0 for v in fill):
        raise ValueError('Invalid receiver')
    if abs(dot(normal,normal)-1)>1e-3: raise ValueError('Normal must be unit length')
    attenuation=hypothesis.rule('attenuation'); a=attenuation.values()
    angle=hypothesis.rule('angle'); spot=hypothesis.rule('spot')
    scale=hypothesis.rule('point_scale').values()['point_scale']
    out=list(fill)
    for (origin,q,axis),visible in zip(sources,visibility):
        if not all(math.isfinite(v) for v in (*origin,*q,*axis)) or any(v<0 for v in q):
            raise ValueError('Invalid source amplitudes/geometry')
        if not visible: continue
        intensity=max(q)
        if not intensity: continue
        delta=sub(origin,position); distance=math.sqrt(dot(delta,delta))
        if not distance: raise ValueError('Coincident source/receiver')
        cosine=dot(normal,delta)/distance
        if cosine<=0: continue  # one-sided receiver; explicit scope restriction
        if angle.selector=='none': cosine=1.
        value=point_scalar(intensity,distance,cosine,linear=attenuation.selector=='linear',
            fade=a['fade'],scale=scale,extra_distance=a['extra_distance_world'],fast=a['fast'],
            tolerance=a['tolerance'],half_lambert=angle.selector=='half_lambert',
            angle_scale=angle.values()['angle_scale'])
        if spot.selector=='q3map_radius16_edge32':
            if abs(dot(axis,axis)-1)>1e-3: raise ValueError('Spot axis must be unit propagation direction')
            p=spot.values(); value*=spot_factor(position,origin,axis,p['target_distance'],p['radius'])
            if a['fast'] and value<=a['tolerance']: value=0.
        for c in range(3): out[c]+=value*q[c]/intensity
    recipe=hypothesis.rule('output').values() if encoding is None else encoding
    return continuous_encode(out,**recipe)


def evaluate_many(hypotheses,*args,**kwargs):
    output=[]
    for h in hypotheses:
        try:
            rgb=evaluate(h,*args,**kwargs)
            output.append(dict(id=h.stable_id,digest=h.digest(),status='RelativeCandidate',rgb=rgb,
                absolute_scale='Unknown',model_discrepancy='Unknown',compiler_identity_verified=False))
        except UnspecifiedModel as e:
            output.append(dict(id=h.stable_id,digest=h.digest(),status='UnspecifiedModel',rgb=None,missing=str(e)))
        except (ValueError,TypeError) as e:
            output.append(dict(id=h.stable_id,digest=h.digest(),status='InvalidInput',rgb=None,error=str(e)))
    return output
