"""Real lightall zero-local-light A/B, retained UBO ABI and interleaved GPU timing.

The original material and vertex shaders are compiled with/without the exact draw
specialization. This deliberately renders retained sun/baked light/emission and
SSR/SSGI outputs; it does not substitute a trivial shader for the reference.
Run with --bench for a synthetic full-screen cost, not a whole-game FPS claim.
"""
import argparse
import ctypes as C
import json
import struct
import numpy as np
from lightrecon_regression import sources, preprocess, cpp_path
from test_watersurface_gl import context, compile_program, gl, SDL, U, I, F, P, tex2d, fbo, read_color, read_depth, vertex_buffer, uloc

OUTPUTS=('out_Color','out_Glow','out_SSRNormal','out_SSRSpecular','out_SSRCubemap','out_SSGIAlbedo','out_SSGIRadiance','out_SkinDiffuse')
ATTRS=('attr_Position','attr_Normal','attr_Color','attr_TexCoord0','attr_TexCoord1','attr_Tangent','attr_LightDirection')
PBR=['USE_LIGHT','USE_NORMALMAP','USE_SPECULARMAP','USE_PHYSICAL_PBR']
SUN=['USE_SHADOWMAP','USE_SHADOWS2','USE_PRIMARY_LIGHT','r_shadowMapSize 512','r_shadowCascadeZFar 4096.0']
CASES=[('fast-lmap',['USE_LIGHT','USE_LIGHTMAP','USE_FAST_LIGHT']),
       ('lmap-shadow',PBR+['USE_LIGHTMAP','USE_DSHADOWS','USE_LTC','USE_LTC_SHADOWS']+SUN),
       ('grid-sun',PBR+['USE_LIGHT_VECTOR','USE_ENTITY_SUN_SPLIT','USE_DSHADOWS']+SUN),
       ('lmap-screen',PBR+['USE_LIGHTMAP','USE_DSHADOWS','USE_SSR','USE_SSGI','USE_SPECULAR_AA']+SUN),
       ('lmap-pom',PBR+['USE_LIGHTMAP','USE_PARALLAXMAP','USE_DSHADOWS']+SUN),
       ('grid-skinned',PBR+['USE_LIGHT_VECTOR','USE_SKELETAL_ANIMATION','USE_ENTITY_SUN_SPLIT','USE_DSHADOWS']+SUN),
       ('lmap-rt-debug',PBR+['USE_LIGHTMAP','USE_RT_DSHADOWS','USE_LIGHTALL_DEBUG','USE_LTC','USE_LTC_DEBUG','USE_FPLUS_DEBUG']+SUN)]

def active(prog):
    count=I();gl('glGetProgramiv',None,U,U,P)(prog,0x8B86,C.byref(count))
    result={}
    for n in range(count.value):
        name=C.create_string_buffer(512);size=I();typ=U()
        gl('glGetActiveUniform',None,U,U,I,P,P,P,P)(prog,n,512,None,C.byref(size),C.byref(typ),name)
        result[name.value.decode()]=(n,size.value,typ.value)
    return result

def block_index(prog,name):return gl('glGetUniformBlockIndex',U,U,C.c_char_p)(prog,name.encode())

def block_layout(prog):
    count=I();gl('glGetProgramiv',None,U,U,P)(prog,0x8A36,C.byref(count));result={}
    for n in range(count.value):
        name=C.create_string_buffer(512);size=I()
        gl('glGetActiveUniformBlockName',None,U,U,I,P,P)(prog,n,512,None,name)
        gl('glGetActiveUniformBlockiv',None,U,U,U,P)(prog,n,0x8A40,C.byref(size))
        result[name.value.decode()]=size.value
    return result

def member_offset(prog,name):
    namep=(C.c_char_p*1)(name.encode());idx=U();offset=I()
    gl('glGetUniformIndices',None,U,I,P,P)(prog,1,namep,C.byref(idx))
    if idx.value==0xffffffff:return None
    gl('glGetActiveUniformsiv',None,U,I,P,U,P)(prog,1,C.byref(idx),0x8A3B,C.byref(offset))
    return offset.value

def bind_blocks(prog):
    eye=np.eye(4,dtype=np.float32).ravel().tolist()
    values={'Camera':dict(u_viewProjectionMatrix=eye,u_ViewOrigin=[0,0,3],u_ViewForward=[0,0,-1],u_ViewInfo=[1,64,0,0],u_FPlusParams2=[0,0,1,1]),
            'Entity':dict(u_ModelMatrix=eye,u_AmbientLight=[.08,.1,.12],u_DirectedLight=[.8,.6,.4],u_FXVolumetricBase=[-1],u_ModelLightDir=[0,0,1]),
            'Scene':dict(u_PrimaryLightOrigin=[0,0,1,0],u_PrimaryLightColor=[.2,.4,.7],u_PBRParams=[.03,.0009,1,1],u_SSGIParams=[7,1,1,1]),
            'Lights':dict(u_ShadowMvp=eye,u_ShadowMvp2=eye,u_ShadowMvp3=eye,u_ShadowSplits=[2,4,8,6],u_ShadowTexelSize=[.125,.125,.125,.125],u_ShadowDepthSpan=[1,1,1,0]),
            'Bones':dict()}
    buffers=[]
    for slot,(name,size) in enumerate(block_layout(prog).items()):
        data=bytearray(size)
        for member,v in values.get(name,{}).items():
            offset=member_offset(prog,member)
            if offset is not None:struct.pack_into('f'*len(v),data,offset,*v)
        b=U();gl('glGenBuffers',None,I,P)(1,C.byref(b));buffers.append(b)
        gl('glBindBuffer',None,U,U)(0x8A11,b)
        raw=C.create_string_buffer(bytes(data));gl('glBufferData',None,U,C.c_ssize_t,P,U)(0x8A11,size,raw,0x88E4)
        gl('glUniformBlockBinding',None,U,U,U)(prog,block_index(prog,name),slot)
        gl('glBindBufferBase',None,U,U,U)(0x8A11,slot,b)
    return buffers

def bind_textures(prog):
    unit=0;resources=[]
    for name,(_,size,typ) in active(prog).items():
        if typ not in (0x8B5E,0x8DC1,0x8DC4,0x8DC2,0x8DD8):continue
        gl('glActiveTexture',None,U)(0x84C0+unit)
        if typ==0x8B5E:
            data=np.empty((8,8,4),np.float32);data[:]=[.4,.5,.6,1]
            data[:,:,0]*=np.linspace(.25,1,8)[None,:]
            if name=='u_NormalMap':data[:]=[.5,.5,1,.5]
            if name=='u_SpecularMap':data[:]=[1,.55,.1,1]
            if name=='u_EnvBrdfMap':data[:]=[.7,.05,.6,1]
            tex=tex2d(8,8,data=data,linear=False)
        elif typ in (0x8DC2,0x8DD8):
            b=U();gl('glGenBuffers',None,I,P)(1,C.byref(b));resources.append(('buffer',b))
            gl('glBindBuffer',None,U,U)(0x8C2A,b);data=np.zeros((16,4),np.float32)
            gl('glBufferData',None,U,C.c_ssize_t,P,U)(0x8C2A,data.nbytes,data.ctypes.data_as(P),0x88E4)
            tex=U();gl('glGenTextures',None,I,P)(1,C.byref(tex));gl('glBindTexture',None,U,U)(0x8C2A,tex)
            gl('glTexBuffer',None,U,U,U)(0x8C2A,0x8814 if typ==0x8DC2 else 0x8236,b)
        else:
            tex=U();gl('glGenTextures',None,I,P)(1,C.byref(tex));gl('glBindTexture',None,U,U)(0x8C1A,tex)
            depth=np.ones((3,8,8),np.float32);depth[:,:4]=.2
            shadow=typ==0x8DC4
            gl('glTexImage3D',None,U,I,I,I,I,I,I,U,U,P)(0x8C1A,0,0x8CAC if shadow else 0x822E,8,8,3,0,0x1902 if shadow else 0x1903,0x1406,depth.ctypes.data_as(P))
            for param in (0x2800,0x2801):gl('glTexParameteri',None,U,U,I)(0x8C1A,param,0x2600)
            if shadow:gl('glTexParameteri',None,U,U,I)(0x8C1A,0x884C,0x884E)
        resources.append(('texture',tex));gl('glUniform1i',None,I,I)(uloc(prog,name),unit);unit+=1
    return resources

def setup(prog):
    gl('glUseProgram',None,U)(prog);buffers=bind_blocks(prog);textures=bind_textures(prog)
    for name,v in dict(u_BaseColor=[1,1,1,1],u_VertColor=[0,0,0,0],u_NormalScale=[1,1,-1,.03],u_SpecularScale=[1,1,1,1],u_PBRMaterial=[1,1,1,0],u_EnableTextures=[0,0,1,0],u_EmissiveParams=[.1,.2,.05,1]).items():
        gl('glUniform4fv',None,I,I,P)(uloc(prog,name),1,np.array(v,np.float32).ctypes.data_as(P))
    for name,value in [('u_LightMask',0),('u_AlphaTestType',0)]:gl('glUniform1i',None,I,I)(uloc(prog,name),value)
    gl('glUniform1f',None,I,F)(uloc(prog,'u_WeatherImpactClass'),-1)
    gl('glUniform2f',None,I,F,F)(uloc(prog,'r_FBufScale'),1/96,1/64)
    return buffers,textures

def capture(prog,fb,w,h):
    gl('glBindFramebuffer',None,U,U)(0x8D40,fb);gl('glViewport',None,I,I,I,I)(0,0,w,h)
    gl('glClearColor',None,F,F,F,F)(0,0,0,0);gl('glClearDepth',None,C.c_double)(1);gl('glClear',None,U)(0x4000|0x0100)
    gl('glUseProgram',None,U)(prog);gl('glDrawArrays',None,U,I,I)(4,0,3)
    result=[read_color(fb,w,h,n) for n in range(7)]
    assert gl('glGetError',U)()==0
    return result,read_depth(fb,w,h)

def bench(prog,fb,w,h):
    gl('glBindFramebuffer',None,U,U)(0x8D40,fb);gl('glViewport',None,I,I,I,I)(0,0,w,h);gl('glDisable',None,U)(0x0B71)
    gl('glUseProgram',None,U)(prog)
    for _ in range(3):gl('glDrawArrays',None,U,I,I)(4,0,3)
    q=U();gl('glGenQueries',None,I,P)(1,C.byref(q));gl('glBeginQuery',None,U,U)(0x88BF,q)
    for _ in range(5):gl('glDrawArrays',None,U,I,I)(4,0,3)
    gl('glEndQuery',None,U)(0x88BF);ns=C.c_uint64();gl('glGetQueryObjectui64v',None,U,U,P)(q,0x8866,C.byref(ns));gl('glDeleteQueries',None,I,P)(1,C.byref(q))
    return ns.value/(w*h*5)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--bench',action='store_true');parser.add_argument('--output');parser.add_argument('--case',choices=[name for name,_ in CASES]);args=parser.parse_args()
    window,ctx=context(b'Exact lightall local-light specialization');report={'driver':[gl('glGetString',C.c_char_p,U)(x).decode() for x in (0x1F00,0x1F01,0x1F02)],'compiled':[],'cases':[]}
    try:
        # Full production vertex processing; the two VS sources must preprocess identically.
        rows=np.array([[-1,-1,.1],[3,-1,.1],[-1,3,.1]],np.float32)
        verts=np.zeros((3,21),np.float32);verts[:,:3]=rows;verts[:,3:6]=[.5,.5,1];verts[:,6:10]=1;verts[:,10:12]=(rows[:,:2]+1)/2;verts[:,12:14]=verts[:,10:12];verts[:,14:18]=[1,.5,.5,1];verts[:,18:21]=[.5,.5,1]
        vertex_buffer(verts,[3,3,4,2,2,4,3])
        cpp=cpp_path();programs=[]
        for name,defines in CASES:
            if args.case and name!=args.case:continue
            pair=[];assembled=[]
            for specialized in (False,True):
                src=sources(dict(program='lightall',defines=defines+(['USE_NO_LOCAL_LIGHTS'] if specialized else [])))
                assembled.append(src)
                prog=compile_program([(0x8B31,src['vertex']),(0x8B30,src['fragment'])],name+str(specialized),attributes=ATTRS,outputs=OUTPUTS);pair.append(prog)
            assert preprocess(assembled[0]['vertex'],cpp)['source']==preprocess(assembled[1]['vertex'],cpp)['source'],name
            a,b=map(block_layout,pair)
            for block,size in b.items():assert a[block]==size,(name,block,a,b)
            for member in active(pair[1]):
                off=member_offset(pair[1],member)
                if off is not None:assert member_offset(pair[0],member)==off,(name,member)
            report['compiled'].append(dict(name=name,defines=defines,vertex_source_identical=True,retained_ubo_offsets_equal=True,original_blocks=a,specialized_blocks=b))
            print('PASS compile/link, identical vertex source, retained std140 ABI:',name,flush=True)
            programs.append((name,pair))
        w,h=96,64
        targets=[tex2d(w,h) for _ in range(7)];depth=tex2d(w,h,internal=0x88F0,fmt=0x84F9,kind=0x84FA);fb=fbo(targets,depth)
        for name,pair in programs:
            if name=='grid-skinned':continue # shader ABI checked; engine handles bone fixture
            images=[]
            for prog in pair:
                setup(prog);gl('glEnable',None,U)(0x0B71);gl('glDepthFunc',None,U)(0x0201);images.append(capture(prog,fb,w,h))
            differences=[]
            # Unwritten MRTs are unspecified. Compare only the production outputs used by this case.
            written=[0,1]+([2,3,4,5,6] if name=='lmap-screen' else [])
            for attachment in written:
                a,b=images[0][0][attachment],images[1][0][attachment]
                assert np.all(np.isfinite(a)) and np.all(np.isfinite(b)),(name,attachment,'nonfinite')
                delta=float(np.max(np.abs(a-b)));assert delta<=2e-6,(name,attachment,delta);differences.append(delta)
            assert np.array_equal(images[0][1],images[1][1]),(name,'depth changed')
            assert np.max(images[0][0][0][:,:,:3])>.01,(name,'empty reference')
            report['cases'].append(dict(name=name,max_float_difference=max(differences),depth_exact=True))
            print('PASS production color/glow/screen outputs and exact depth:',name,max(differences),flush=True)
            if name=='lmap-shadow':
                # Negative controls prove this fixture actually exercises the
                # retained light and emission, beyond a matching empty output.
                prog=pair[0];controls={};reference=images[0][0][0]
                setup(prog)
                gl('glUniform4f',None,I,F,F,F,F)(uloc(prog,'u_EmissiveParams'),0,0,0,0)
                controls['emission']=float(np.max(np.abs(reference-capture(prog,fb,w,h)[0][0])))
                buffers,_=setup(prog)
                scene=buffers[list(block_layout(prog)).index('Scene')]
                gl('glBindBuffer',None,U,U)(0x8A11,scene)
                zero=np.zeros(3,np.float32)
                gl('glBufferSubData',None,U,C.c_ssize_t,C.c_ssize_t,P)(0x8A11,member_offset(prog,'u_PrimaryLightColor'),zero.nbytes,zero.ctypes.data_as(P))
                controls['sun']=float(np.max(np.abs(reference-capture(prog,fb,w,h)[0][0])))
                setup(prog)
                unit=I();gl('glGetUniformiv',None,U,I,P)(prog,uloc(prog,'u_LightMap'),C.byref(unit))
                gl('glActiveTexture',None,U)(0x84C0+unit.value)
                tex2d(1,1,data=np.zeros(4,np.float32))
                controls['baked']=float(np.max(np.abs(reference-capture(prog,fb,w,h)[0][0])))
                assert all(v>1e-4 for v in controls.values()),controls
                report['retained_feature_controls']=controls
                print('PASS nonzero retained baked/sun/emission controls:',controls,flush=True)

        if args.bench:
            report['benchmark_resolution']=[512,512]
            w,h=512,512;fb=fbo([tex2d(w,h) for _ in range(7)])
            for name,pair in programs:
                if name not in ('lmap-shadow','grid-sun','lmap-screen'):continue
                times=[[],[]]
                for order in [(0,1),(1,0),(0,1),(1,0),(0,1)]:
                    for n in order:setup(pair[n]);times[n].append(bench(pair[n],fb,w,h))
                medians=[float(np.median(t)) for t in times]
                report.setdefault('benchmark',[]).append(dict(name=name,ns_per_fragment=medians,samples=times))
                print('BENCH ns/fragment original/specialized:',name,medians,flush=True)
        assert gl('glGetError',U)()==0
        if args.output:
            from pathlib import Path
            Path(args.output).write_text(json.dumps(report,indent=2))
    finally:SDL.SDL_GL_DeleteContext(ctx);SDL.SDL_DestroyWindow(window);SDL.SDL_Quit()
if __name__=='__main__':main()
