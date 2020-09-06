/*

  -mfmt=<format>    Output SPIR-V binary code using the selected format. This
                    option may be specified only when the compilation output is
                    in SPIR-V binary code form. Available options are:
                      bin   - SPIR-V binary words.  This is the default.
                      c     - Binary words as C initializer list of 32-bit ints
                      num   - List of comma-separated 32-bit hex integers
					  
  -O                Optimize the generated SPIR-V code for better performance.
  -Os               Optimize the generated SPIR-V code for smaller size.
  -O0               Disable optimization.
  -o <file>         Write output to <file>.
  
%VULKAN_SDK%\Bin\glslc.exe -Os -o hello.spv hello.vert
%VULKAN_SDK%\Bin\spirv-dis hello.spv

*/
#version 450 core

layout(std430, push_constant) uniform PushConstants {
    vec4 m; // mat2, [0] = .xy, [1] = .zw
	vec4 translation; // .xy only, then pad out the rest (IDK if needed?)
} pc;

layout(location = 0) out vec3 color;

void main()
{
	uint vid = gl_VertexIndex;
	
	color = vec3(
		vid == 0 ? 1 : 0,
		vid == 1 ? 1 : 0,
		vid == 2 ? 1 : 0
	);
	
	vec2 p = vec2(
		float(vid & 1u) * 0.50f, // 0, .5, 0)
		float(vid & 2u) * 0.25f // 0, 0, .5
	);
	
	mat2 rotScale = mat2(pc.m.xy, pc.m.zw);
	gl_Position = vec4(rotScale * p + pc.translation.xy, 0, 1.0f);
}