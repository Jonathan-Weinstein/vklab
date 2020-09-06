#version 450 core

layout(location = 0) in vec3 color;

layout(location = 0) out vec4 attatchment0;

void main()
{
	attatchment0 = vec4(gl_FrontFacing ? color : vec3(1,1,1) - color, 1);
}
