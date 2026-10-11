#pragma once

#include "FoveatedMaskCalibration.h"

namespace FoveatedHeadsetGeometry
{
	using FoveatedMaskCalibration::Projection;

	/** Build direction-only eye UV mapping from OpenVR optics, without scene-camera state or IPD parallax. */
	[[nodiscard]] inline std::optional<Projection> BuildProjection(const std::array<std::array<float, 4>, 2>& tangents,
		const std::array<Projection, 2>& rotations)
	{
		std::array<Projection, 2> eyeToHead;
		for (unsigned eye = 0; eye < 2; ++eye) {
			const auto& t = tangents[eye];
			for (float value : t)
				if (!std::isfinite(value) || std::abs(value) > 10.0f)
					return std::nullopt;
			if (t[1] - t[0] < 1e-4f || t[3] - t[2] < 1e-4f)
				return std::nullopt;
			const auto& rotation = rotations[eye];
			const auto inverseRotation = FoveatedMaskCalibration::Inverse(rotation);
			if (!inverseRotation)
				return std::nullopt;
			for (unsigned row = 0; row < 3; ++row)
				for (unsigned column = 0; column < 3; ++column)
					if (std::abs((*inverseRotation)[row * 3 + column] - rotation[column * 3 + row]) > 1e-3)
						return std::nullopt;
			// OpenVR returns bottom then top; eye UV is top-down and view forward is -Z.
			const Projection uvToEye{ t[1] - t[0], 0, t[0], 0, t[2] - t[3], t[3], 0, 0, -1 };
			eyeToHead[eye] = FoveatedMaskCalibration::Multiply(rotation, uvToEye);
		}
		const auto headToRight = FoveatedMaskCalibration::Inverse(eyeToHead[1]);
		if (!headToRight)
			return std::nullopt;
		const auto projection = FoveatedMaskCalibration::Multiply(*headToRight, eyeToHead[0]);
		return FoveatedMaskCalibration::Inverse(projection) ? std::optional{ projection } : std::nullopt;
	}

	/** Read the most inward visible mesh extent; absent meshes conservatively use the full eye. */
	template <class Vertex>
	[[nodiscard]] inline std::optional<float> InnerLimit(const Vertex* vertices, uint32_t count, unsigned eye)
	{
		if (eye > 1 || count > 12288 || (count && !vertices))
			return std::nullopt;
		if (!count)
			return eye == 0 ? 1.0f : 0.0f;
		std::array<float, 2> minimum{ 1, 1 }, maximum{ 0, 0 };
		for (uint32_t vertex = 0; vertex < count; ++vertex) {
			for (unsigned axis = 0; axis < 2; ++axis) {
				const float value = vertices[vertex].v[axis];
				if (!std::isfinite(value) || value < -1e-4f || value > 1.0001f)
					return std::nullopt;
				minimum[axis] = std::min(minimum[axis], value);
				maximum[axis] = std::max(maximum[axis], value);
			}
		}
		if (maximum[0] - minimum[0] < 1e-4f || maximum[1] - minimum[1] < 1e-4f)
			return std::nullopt;
		return std::clamp(eye == 0 ? maximum[0] : minimum[0], 0.0f, 1.0f);
	}

	/** Copy a conservative convex visible outline; absent meshes cover the complete eye. */
	template <class Vertex>
	[[nodiscard]] inline std::optional<FoveatedMaskCalibration::Polygon> VisibleOutline(const Vertex* vertices, uint32_t count)
	{
		using FoveatedMaskCalibration::Polygon;
		if (!InnerLimit(vertices, count, 0))
			return std::nullopt;
		if (!count)
			return Polygon{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		Polygon points;
		points.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
			points.push_back({ std::clamp(double(vertices[i].v[0]), 0.0, 1.0), std::clamp(double(vertices[i].v[1]), 0.0, 1.0) });
		auto outline = FoveatedMaskCalibration::ConvexHull(std::move(points));
		if (outline.size() > FoveatedMaskCalibration::kMaximumHeadsetOutlineVertices) {
			Polygon bounds{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			// Circumscribed planes retain all visible pixels while bounding persisted contour size.
			for (unsigned i = 0; i < 64; ++i) {
				const double angle = i * 6.283185307179586 / 64;
				const double nx = std::cos(angle), ny = std::sin(angle);
				double support = -2;
				for (auto point : outline)
					support = std::max(support, nx * point.x + ny * point.y);
				bounds = FoveatedMaskCalibration::ClipHalfPlane(std::move(bounds), -nx, -ny, support);
			}
			outline = std::move(bounds);
		}
		return FoveatedMaskCalibration::IsValidHeadsetOutline(outline) ? std::optional{ std::move(outline) } : std::nullopt;
	}

	/** Encode an eye-UV homography for the preview shader's column-vector clip transform. */
	template <class ClipMatrix>
	[[nodiscard]] inline ClipMatrix ToClipProjection(const Projection& p)
	{
		return {
			float(p[0] - p[6] * 0.5), float(-p[1] + p[7] * 0.5), 0.0f, float(p[0] + p[1] + 2 * p[2] - (p[6] + p[7]) * 0.5 - p[8]),
			float(-p[3] + p[6] * 0.5), float(p[4] - p[7] * 0.5), 0.0f, float(-p[3] - p[4] - 2 * p[5] + (p[6] + p[7]) * 0.5 + p[8]),
			0.0f, 0.0f, 1.0f, 0.0f,
			float(p[6] * 0.5), float(-p[7] * 0.5), 0.0f, float((p[6] + p[7]) * 0.5 + p[8])
		};
	}
}
