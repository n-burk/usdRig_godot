shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform sampler2D ball_texture : source_color, filter_linear_mipmap, repeat_disable;
uniform vec3 diffuse_scale = vec3(0.75);
uniform vec3 emission_scale = vec3(0.5);
uniform float roughness = 0.5;
uniform float metallic = 0.0;
uniform float specular = 0.35;

void fragment() {
	// UsdUVTexture: repeat S, clamp T. OBJ import has already flipped V.
	vec2 st = vec2(fract(UV.x), clamp(UV.y, 0.0, 1.0));
	vec3 texel = texture(ball_texture, st).rgb;
	ALBEDO = texel * diffuse_scale;
	EMISSION = texel * emission_scale;
	ROUGHNESS = roughness;
	METALLIC = metallic;
	SPECULAR = specular;
}
