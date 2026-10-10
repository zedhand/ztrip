// The demo's centrepiece. `glow` follows the bass and `pulse` is the beat
// phase (0 on the beat, rising to 1 just before the next one).
shader_type spatial;

uniform vec4 base_color : source_color = vec4(0.15, 0.45, 1.0, 1.0);
uniform float glow = 0.5;
uniform float pulse = 0.0;

void fragment() {
	float facing = clamp(dot(NORMAL, VIEW), 0.0, 1.0);
	float rim = pow(1.0 - facing, 2.0);
	float hit = pow(1.0 - clamp(pulse, 0.0, 1.0), 8.0);
	ALBEDO = base_color.rgb * 0.15;
	ROUGHNESS = 0.3;
	EMISSION = base_color.rgb * glow * (0.5 + 0.5 * rim) + vec3(1.0, 0.85, 0.6) * hit * rim * 3.0;
}
