"""Driver compile/ABI tests and exact production generic instanced draw A/B.

Uses the real MD3 library and unchanged material source; tests transforms, uniform
scale, lighting origins' resulting constants, fragment indexing and 32 instances.
"""
import ctypes as C
import json
import numpy as np
from lightrecon_regression import common_header, sources, stage, enum_values, read
from test_watersurface_gl import context, compile_program, gl, SDL, U, I, F, P, uniform_block, uloc

ATTRS=('attr_Position','attr_Normal','attr_Color','attr_TexCoord0')

def assemble(case, instanced):
    result=sources(case)
    if instanced:
        result={kind: text.split('\n',1)[0]+'\n#define USE_MD3_INSTANCING\n'+stage('md3_instance',kind.title())+'\n'+text.split('\n',1)[1]
                for kind,text in result.items()}
    return [(0x8B31 if kind=='vertex' else 0x8B30,text) for kind,text in result.items()]

def bind_data(program, blocks):
    gl('glUseProgram',None,U)(program)
    for name, slot, data in blocks: uniform_block(program,name,slot,data)
    gl('glUniform1i',None,I,I)(uloc(program,'u_PlantBones'),1)
    gl('glUniform1f',None,I,F)(uloc(program,'u_WeatherImpactClass'),-1)
    colors=enum_values(read('shared/rd-rend2/tr_local.h'),'colorGen_t')
    gl('glUniform1i',None,I,I)(uloc(program,'u_ColorGen'),colors['CGEN_LIGHTING_DIFFUSE'])
    gl('glUniform4f',None,I,F,F,F,F)(uloc(program,'u_BaseColor'),0,0,0,1)
    gl('glUniform4f',None,I,F,F,F,F)(uloc(program,'u_VertColor'),0,0,0,0)


def draw_ab(ordinary, instanced, n):
    camera=np.zeros(64,np.float32); camera[:16]=np.eye(4,dtype=np.float32).ravel()
    entity=np.zeros((32,84),np.float32)
    for index in range(n):
        row,col=divmod(index,8)
        angle=index*.11; scale=.07*(1+(index%3)*.15)
        m=np.eye(4,dtype=np.float32)
        m[:2,:2]=np.array([[np.cos(angle),-np.sin(angle)],[np.sin(angle),np.cos(angle)]])*scale
        m[2,2]=scale; m[0,3]=-.875+col*.25; m[1,3]=-.75+row*.5
        entity[index,:16]=m.T.ravel()
        entity[index,20:23]=[.02+index*.003,.04,.08]
        entity[index,24:27]=[.1+index*.01,.3-index*.005,.16]
        entity[index,27]=-1 # fxVolumetricBase; zero enables a different effect
        entity[index,28:31]=[0,0,1]
        # Full-grid / SH and split-sun payload must preserve the std140 stride.
        entity[index,32:84]=index*.001
    vao=U();gl('glGenVertexArrays',None,I,P)(1,C.byref(vao));gl('glBindVertexArray',None,U)(vao)
    vertices=np.array([[-1,-1,0,.5,.5,1,1,1,1,1,0,0],[1,-1,0,.5,.5,1,1,1,1,1,1,0],[0,1,0,.5,.5,1,1,1,1,1,.5,1]],np.float32)
    buffer=U();gl('glGenBuffers',None,I,P)(1,C.byref(buffer));gl('glBindBuffer',None,U,U)(0x8892,buffer)
    gl('glBufferData',None,U,C.c_ssize_t,P,U)(0x8892,vertices.nbytes,vertices.ctypes.data_as(P),0x88E4)
    for a,(size,offset) in enumerate([(3,0),(3,12),(4,24),(2,40)]):
        gl('glEnableVertexAttribArray',None,U)(a)
        gl('glVertexAttribPointer',None,U,I,U,U,I,P)(a,size,0x1406,0,48,P(offset))
    ibo=U();gl('glGenBuffers',None,I,P)(1,C.byref(ibo));gl('glBindBuffer',None,U,U)(0x8893,ibo)
    indices=np.array([0,1,2],np.uint32)
    gl('glBufferData',None,U,C.c_ssize_t,P,U)(0x8893,indices.nbytes,indices.ctypes.data_as(P),0x88E4)
    texture=U();gl('glGenTextures',None,I,P)(1,C.byref(texture));gl('glBindTexture',None,U,U)(0x0DE1,texture)
    pixel=np.ones(4,np.float32)
    gl('glTexImage2D',None,U,I,I,I,I,I,U,U,P)(0x0DE1,0,0x8814,1,1,0,0x1908,0x1406,pixel.ctypes.data_as(P))
    for param in (0x2801,0x2800):gl('glTexParameteri',None,U,U,I)(0x0DE1,param,0x2600)
    gl('glViewport',None,I,I,I,I)(0,0,32,32)
    gl('glDisable',None,U)(0x0BD0) # dither
    def capture(program, batching):
        gl('glClearColor',None,F,F,F,F)(0,0,0,1);gl('glClear',None,U)(0x4000)
        bind_data(program,[('Camera',0,camera),('Scene',1,np.zeros(32,np.float32))])
        if batching:
            uniform_block(program,'Md3Instances',15,entity)
            gl('glDrawElementsInstancedBaseVertex',None,U,I,U,P,I,I)(4,3,0x1405,None,n,0)
        else:
            for index in range(n):
                uniform_block(program,'Entity',4,entity[index])
                gl('glDrawElementsInstancedBaseVertex',None,U,I,U,P,I,I)(4,3,0x1405,None,1,0)
        image=np.zeros((32,32,4),np.uint8)
        gl('glReadPixels',None,I,I,I,I,U,U,P)(0,0,32,32,0x1908,0x1401,image.ctypes.data_as(P))
        assert gl('glGetError',U)()==0
        return image
    reference=capture(ordinary,False); actual=capture(instanced,True)
    assert np.any(reference[:,:,:3]),'no rendered geometry'
    assert np.array_equal(reference,actual),(n,int(np.max(np.abs(reference.astype(int)-actual.astype(int)))))
    colors={tuple(c) for c in actual[:,:,:3].reshape(-1,3) if np.any(c)}
    assert len(colors)>=min(n,8),('individual lighting lost',len(colors))
    print(f'PASS production generic exact A/B: {n} transforms/lights, {n} -> 1 GL indexed draws')


def main():
    window,ctx=context(b'MD3 instancing correctness')
    try:
        cases=[dict(name='generic-rgb',program='generic',defines=['USE_RGBAGEN']),
               dict(name='generic-opaque',program='generic',defines=[]),
               dict(name='lightall-grid',program='lightall',defines=['USE_LIGHT','USE_LIGHT_VECTOR']),
               dict(name='lightall-grid-pbr',program='lightall',defines=['USE_LIGHT','USE_LIGHT_VECTOR','USE_NORMALMAP','USE_SPECULARMAP','USE_PHYSICAL_PBR','USE_ENTITY_SUN_SPLIT','USE_SHADOWMAP','SHADOWMAP_MODULATE','r_shadowMapSize 512','r_shadowCascadeZFar 4096.0']),
               dict(name='lightall-entity-grid',program='lightall',defines=['USE_LIGHT','USE_LIGHT_VECTOR','USE_ENTITY_GRID','ENTITY_GRID_LDR_RANGE 4.0']),
               dict(name='lightall-sh',program='lightall',defines=['USE_LIGHT','USE_LIGHT_VECTOR','USE_RECON_ENTITY_IRRADIANCE','USE_ENTITY_SUN_SPLIT','USE_SHADOWMAP','SHADOWMAP_MODULATE','r_shadowMapSize 512','r_shadowCascadeZFar 4096.0'])]
        programs=[]
        for case in cases:
            pair=[]
            for instanced in (False,True):
                program=compile_program(assemble(case,instanced),case['name']+str(instanced),attributes=ATTRS)
                index=gl('glGetUniformBlockIndex',U,U,C.c_char_p)(program,b'Md3Instances')
                if instanced:
                    assert index!=0xffffffff
                    size=I();gl('glGetActiveUniformBlockiv',None,U,U,U,P)(program,index,0x8A40,C.byref(size))
                    assert size.value==32*336,size.value
                else:assert index==0xffffffff,'ordinary shader acquired instancing data'
                pair.append(program)
            programs.append(pair)
            print('PASS compile/ABI ordinary + instanced:',case['name'])
        for n in (2,17,32):draw_ab(*programs[0],n)
        assert gl('glGetError',U)()==0
    finally:
        SDL.SDL_GL_DeleteContext(ctx);SDL.SDL_DestroyWindow(window);SDL.SDL_Quit()
if __name__=='__main__':main()
