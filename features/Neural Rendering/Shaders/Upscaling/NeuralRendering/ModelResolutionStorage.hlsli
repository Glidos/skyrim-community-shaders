float RoundUnsignedModelStorage(float value, uint mantissaBits)
{
	if (value < asfloat(0x38800000u)) {
		const float scale = float(1u << (14u + mantissaBits));
		return floor(value * scale) / scale;
	}
	const uint shift = 23u - mantissaBits;
	const uint mask = (1u << shift) - 1u;
	// Typed lower-range FLOAT stores truncate rather than use arithmetic rounding.
	const uint bits = asuint(value) & ~mask;
	return asfloat(bits);
}

float4 RoundModelReconstruction(float4 color, uint format)
{
	// Match the removed typed UAV store before fractional actor selection.
	if (format == 10u)
		return f16tof32(f32tof16(color));
	if (format == 26u)
		return float4(RoundUnsignedModelStorage(color.r, 6u), RoundUnsignedModelStorage(color.g, 6u),
			RoundUnsignedModelStorage(color.b, 5u), 1.0);
	float4 maximum = format == 28u ? 255.0.xxxx :
	                 format == 24u ? float4(1023.0, 1023.0, 1023.0, 3.0) :
	                                 65535.0.xxxx;
	return format == 2u ? color : round(saturate(color) * maximum) / maximum;
}
