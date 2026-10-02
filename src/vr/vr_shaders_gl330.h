#ifndef VR_SHADERS_GL330_H
#define VR_SHADERS_GL330_H

// GLSL 330 core variants of render_gl.c's shaders, for the VR build.
//
// SteamVR on the Steam Frame needs OpenGL 4.3 or newer (xrGetOpenGLGraphics
// RequirementsKHR reports 4.3 to 4.6), and the Frame's GL is Zink, which
// F-Zero X measured as a 4.6 *core* context. So the VR build asks SDL for a
// core context and these replace the GLSL 1.10 shaders (attribute, varying,
// gl_FragColor, texture2D), which a core context does not compile. The bodies
// are the same. The sampler is called `tex`, because `texture` is a built-in
// function in 330.

#define SHADER_SOURCE_330(...) "#version 330 core\n" #__VA_ARGS__

static const char * const SHADER_GAME_VS = SHADER_SOURCE_330(
	in vec3 pos;
	in vec2 uv;
	in vec4 color;

	out vec4 v_color;
	out vec2 v_uv;
	uniform mat4 view;
	uniform mat4 model;
	uniform mat4 projection;
	uniform vec2 screen;
	uniform vec3 camera_pos;
	uniform vec2 fade;
	uniform float time;
	
	void main(void) {
		gl_Position = projection * view * model * vec4(pos, 1.0);
		gl_Position.xy += screen.xy * gl_Position.w;
		v_color = color;
		v_color.a *= smoothstep(
			fade.y, fade.x, // fadeout far, near
			length(vec4(camera_pos, 1.0) - model * vec4(pos, 1.0))
		);
		v_uv = uv / 2048.0; // ATLAS_GRID * ATLAS_SIZE
	}
);

static const char * const SHADER_GAME_FS = SHADER_SOURCE_330(
	in vec4 v_color;
	in vec2 v_uv;
	uniform sampler2D tex;
	out vec4 frag_color;

	void main(void) {
		vec4 tex_color = texture(tex, v_uv);
		vec4 color = tex_color * v_color;
		if (color.a == 0.0) {
			discard;
		}
		color.rgb = color.rgb * 2.0;
		frag_color = color;
	}
);

static const char * const SHADER_POST_VS = SHADER_SOURCE_330(
	in vec3 pos;
	in vec2 uv;

	out vec2 v_uv;

	uniform mat4 projection;
	uniform vec2 screen_size;
	uniform float time;
	
	void main(void) {
		gl_Position = projection * vec4(pos, 1.0);
		v_uv = uv;
	}
);

static const char * const SHADER_POST_FS_DEFAULT = SHADER_SOURCE_330(
	in vec2 v_uv;

	uniform sampler2D tex;
	uniform vec2 screen_size;
	out vec4 frag_color;

	void main(void) {
		frag_color = texture(tex, v_uv);
	}
);

// CRT effect based on https://www.shadertoy.com/view/Ms23DR 
// by https://github.com/mattiasgustavsson/
static const char * const SHADER_POST_FS_CRT = SHADER_SOURCE_330(
	in vec2 v_uv;

	uniform float time;
	uniform sampler2D tex;
	uniform vec2 screen_size;
	out vec4 frag_color;

	vec2 curve(vec2 uv) {
		uv = (uv - 0.5) * 2.0;
		uv *= 1.1;	
		uv.x *= 1.0 + pow((abs(uv.y) / 5.0), 2.0);
		uv.y *= 1.0 + pow((abs(uv.x) / 4.0), 2.0);
		uv  = (uv / 2.0) + 0.5;
		uv =  uv *0.92 + 0.04;
		return uv;
	}

	void main(){
		vec2 uv = curve(gl_FragCoord.xy / screen_size);
		vec3 color;
		float x =  sin(0.3*time+uv.y*21.0)*sin(0.7*time+uv.y*29.0)*sin(0.3+0.33*time+uv.y*31.0)*0.0017;

		color.r = texture(tex, vec2(x+uv.x+0.001,uv.y+0.001)).x+0.05;
		color.g = texture(tex, vec2(x+uv.x+0.000,uv.y-0.002)).y+0.05;
		color.b = texture(tex, vec2(x+uv.x-0.002,uv.y+0.000)).z+0.05;
		color.r += 0.08*texture(tex, 0.75*vec2(x+0.025, -0.027)+vec2(uv.x+0.001,uv.y+0.001)).x;
		color.g += 0.05*texture(tex, 0.75*vec2(x+-0.022, -0.02)+vec2(uv.x+0.000,uv.y-0.002)).y;
		color.b += 0.08*texture(tex, 0.75*vec2(x+-0.02, -0.018)+vec2(uv.x-0.002,uv.y+0.000)).z;

		color = clamp(color*0.6+0.4*color*color*1.0,0.0,1.0);

		float vignette = (0.0 + 1.0*16.0*uv.x*uv.y*(1.0-uv.x)*(1.0-uv.y));
		color *= vec3(pow(vignette, 0.25));

		color *= vec3(0.95,1.05,0.95);
		color *= 2.8;

		float scanlines = clamp( 0.35+0.35*sin(3.5*time+uv.y*screen_size.y*1.5), 0.0, 1.0);
		
		float s = pow(scanlines,1.7);
		color = color * vec3(0.4+0.7*s);

		color *= 1.0+0.01*sin(110.0*time);
		if (uv.x < 0.0 || uv.x > 1.0)
			color *= 0.0;
		if (uv.y < 0.0 || uv.y > 1.0)
			color *= 0.0;
		
		color*=1.0-0.65*vec3(clamp((mod(gl_FragCoord.x, 2.0)-1.0)*2.0,0.0,1.0));
		frag_color = vec4(color,1.0);
	}
);

#endif
