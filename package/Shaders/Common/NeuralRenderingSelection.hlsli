#ifndef CSX_NEURAL_RENDERING_SELECTION
#define CSX_NEURAL_RENDERING_SELECTION

float NeuralRenderingSelectionWeight(float mask, uint mode)
{
	float weight = mask;
	// Native provider blending has already applied the fractional strength.
	return mode == 2u && weight > 0.0 ? 1.0 : weight;
}

#endif
