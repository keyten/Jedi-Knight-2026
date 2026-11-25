/*[Vertex]*/
flat out int var_Md3Instance;
#define MD3_INSTANCE_INDEX gl_InstanceID
struct Md3Entity
{
 mat4 modelMatrix;
 vec4 lightOrigin;
 vec3 ambientLight; float entityTime;
 vec3 directedLight; float fxVolumetricBase;
 vec3 modelLightDir; float vertexLerp;
 vec4 gridAmbient[3];
 vec4 gridDirected[3];
 vec4 gridDirection[3];
 vec4 gridParams;
 vec4 gridMinimum;
 vec4 gridScale;
 vec4 entitySun;
};
layout(std140) uniform Md3Instances { Md3Entity u_Md3Entities[32]; };
#define u_ModelMatrix u_Md3Entities[MD3_INSTANCE_INDEX].modelMatrix
#define u_LocalLightOrigin u_Md3Entities[MD3_INSTANCE_INDEX].lightOrigin
#define u_AmbientLight u_Md3Entities[MD3_INSTANCE_INDEX].ambientLight
#define u_entityTime u_Md3Entities[MD3_INSTANCE_INDEX].entityTime
#define u_DirectedLight u_Md3Entities[MD3_INSTANCE_INDEX].directedLight
#define u_FXVolumetricBase u_Md3Entities[MD3_INSTANCE_INDEX].fxVolumetricBase
#define u_ModelLightDir u_Md3Entities[MD3_INSTANCE_INDEX].modelLightDir
#define u_VertexLerp u_Md3Entities[MD3_INSTANCE_INDEX].vertexLerp
#define u_GridAmbient u_Md3Entities[MD3_INSTANCE_INDEX].gridAmbient
#define u_GridDirected u_Md3Entities[MD3_INSTANCE_INDEX].gridDirected
#define u_GridDirection u_Md3Entities[MD3_INSTANCE_INDEX].gridDirection
#define u_GridParams u_Md3Entities[MD3_INSTANCE_INDEX].gridParams
#define u_GridMinimum u_Md3Entities[MD3_INSTANCE_INDEX].gridMinimum
#define u_GridScale u_Md3Entities[MD3_INSTANCE_INDEX].gridScale
#define u_EntitySun u_Md3Entities[MD3_INSTANCE_INDEX].entitySun

/*[Fragment]*/
flat in int var_Md3Instance;
#define MD3_INSTANCE_INDEX var_Md3Instance
struct Md3Entity
{
 mat4 modelMatrix;
 vec4 lightOrigin;
 vec3 ambientLight; float entityTime;
 vec3 directedLight; float fxVolumetricBase;
 vec3 modelLightDir; float vertexLerp;
 vec4 gridAmbient[3];
 vec4 gridDirected[3];
 vec4 gridDirection[3];
 vec4 gridParams;
 vec4 gridMinimum;
 vec4 gridScale;
 vec4 entitySun;
};
layout(std140) uniform Md3Instances { Md3Entity u_Md3Entities[32]; };
#define u_ModelMatrix u_Md3Entities[MD3_INSTANCE_INDEX].modelMatrix
#define u_LocalLightOrigin u_Md3Entities[MD3_INSTANCE_INDEX].lightOrigin
#define u_AmbientLight u_Md3Entities[MD3_INSTANCE_INDEX].ambientLight
#define u_entityTime u_Md3Entities[MD3_INSTANCE_INDEX].entityTime
#define u_DirectedLight u_Md3Entities[MD3_INSTANCE_INDEX].directedLight
#define u_FXVolumetricBase u_Md3Entities[MD3_INSTANCE_INDEX].fxVolumetricBase
#define u_ModelLightDir u_Md3Entities[MD3_INSTANCE_INDEX].modelLightDir
#define u_VertexLerp u_Md3Entities[MD3_INSTANCE_INDEX].vertexLerp
#define u_GridAmbient u_Md3Entities[MD3_INSTANCE_INDEX].gridAmbient
#define u_GridDirected u_Md3Entities[MD3_INSTANCE_INDEX].gridDirected
#define u_GridDirection u_Md3Entities[MD3_INSTANCE_INDEX].gridDirection
#define u_GridParams u_Md3Entities[MD3_INSTANCE_INDEX].gridParams
#define u_GridMinimum u_Md3Entities[MD3_INSTANCE_INDEX].gridMinimum
#define u_GridScale u_Md3Entities[MD3_INSTANCE_INDEX].gridScale
#define u_EntitySun u_Md3Entities[MD3_INSTANCE_INDEX].entitySun
