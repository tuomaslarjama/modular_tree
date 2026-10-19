#include "LeafShapeGenerator.hpp"
#include "VenationGenerator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Mtree
{

static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = 2.0f * PI;

float LeafShapeGenerator::superformula_radius(float theta, float effective_n1) const
{
	float ct = std::cos(m * theta / 4.0f);
	float st = std::sin(m * theta / 4.0f);

	float term1 = std::pow(std::abs(ct / a), n2);
	float term2 = std::pow(std::abs(st / b), n3);

	float sum = term1 + term2;
	if (sum < 1e-10f)
		return 1.0f;

	float result = std::pow(sum, -1.0f / effective_n1);
	if (!std::isfinite(result))
		return 1.0f;
	return result;
}

static float signed_angle_from(float theta, float reference)
{
	float difference = std::fmod(theta - reference + PI, TWO_PI);
	if (difference < 0.0f)
		difference += TWO_PI;
	return difference - PI;
}

float LeafShapeGenerator::base_and_tip_scale(float theta) const
{
	const float base_angle = 1.5f * PI; // leaf base points down (-Y)
	const float tip_angle = 0.5f * PI;  // leaf tip points up (+Y)
	float scale = 1.0f;

	float from_base = signed_angle_from(theta, base_angle);
	if (base_notch_depth > 0.0f && std::abs(from_base) < base_notch_width)
	{
		float falloff = 1.0f - std::abs(from_base) / base_notch_width;
		scale *= 1.0f - base_notch_depth * falloff * falloff;
	}
	if (base_asymmetry != 0.0f && std::abs(from_base) < 0.5f * PI)
	{
		// sin * cos^2 is zero at the notch and at the leaf sides, so one basal lobe
		// grows and the other shrinks without breaking the outline. 2.6 scales its peak to ~1.
		float cos_base = std::cos(from_base);
		scale *= 1.0f + base_asymmetry * 2.6f * std::sin(from_base) * cos_base * cos_base;
	}

	float from_tip = signed_angle_from(theta, tip_angle);
	if (tip_length > 0.0f && std::abs(from_tip) < tip_width)
	{
		float falloff = 1.0f - std::abs(from_tip) / tip_width;
		scale *= 1.0f + tip_length * falloff * falloff;
	}
	return scale;
}

Vector2 LeafShapeGenerator::contour_point(float theta, float effective_n1) const
{
	float r = superformula_radius(theta, effective_n1) * base_and_tip_scale(theta);
	return Vector2(r * std::cos(theta) * aspect_ratio, r * std::sin(theta));
}

std::vector<Vector2> LeafShapeGenerator::sample_contour(size_t& base_index)
{
	// Multiple of 4 so the exact base (3/4 turn) and tip (1/4 turn) angles get a sample
	int res = std::max(contour_resolution, 8);
	res = (res + 3) / 4 * 4;

	// Sample base contour points
	std::vector<Vector2> points;
	points.reserve(res * 2);

	float clamped_n1 = (std::abs(n1) < 0.001f) ? 0.001f : n1;

	for (int i = 0; i < res; ++i)
	{
		float theta = TWO_PI * static_cast<float>(i) / static_cast<float>(res);
		points.push_back(contour_point(theta, clamped_n1));
	}

	// Adaptive refinement: subdivide segments with high curvature
	std::vector<Vector2> refined;
	refined.reserve(points.size() * 2);
	size_t base_sample = static_cast<size_t>(res) * 3 / 4;

	for (size_t i = 0; i < points.size(); ++i)
	{
		size_t prev = (i == 0) ? points.size() - 1 : i - 1;
		size_t next = (i + 1) % points.size();

		if (i == base_sample)
			base_index = refined.size();
		refined.push_back(points[i]);

		// Check curvature deviation between this segment and the next
		Vector2 d1 = points[i] - points[prev];
		Vector2 d2 = points[next] - points[i];
		d1.normalize();
		d2.normalize();
		float dot = d1.dot(d2);

		// If angle deviation is significant, add midpoint
		if (dot < 0.95f)
		{
			float theta_mid = TWO_PI * (static_cast<float>(i) + 0.5f) / static_cast<float>(res);
			refined.push_back(contour_point(theta_mid, clamped_n1));
		}
	}

	return refined;
}

std::vector<Vector2> LeafShapeGenerator::apply_margin(const std::vector<Vector2>& contour)
{
	if (margin_type == MarginType::Entire || tooth_count <= 0)
	{
		return contour;
	}

	std::mt19937 rng(asymmetry_seed);
	std::uniform_real_distribution<float> asym_dist(-0.3f, 0.3f);

	std::vector<Vector2> result;
	result.reserve(contour.size());

	for (size_t i = 0; i < contour.size(); ++i)
	{
		const Vector2& pt = contour[i];
		float r = pt.norm();
		if (r < 1e-10f)
		{
			result.push_back(pt);
			continue;
		}

		float theta = std::atan2(pt.y(), pt.x());
		if (theta < 0.0f)
			theta += TWO_PI;

		float t = theta * static_cast<float>(tooth_count) / TWO_PI;
		float frac = t - std::floor(t);
		float asym_offset = (asymmetry_seed != 0) ? asym_dist(rng) : 0.0f;
		float depth = tooth_depth * (1.0f + asym_offset);
		float mod = 0.0f;

		switch (margin_type)
		{
		case MarginType::Serrate:
		{
			// Asymmetric sawtooth wave (teeth point toward tip)
			float safe_sharpness = std::clamp(tooth_sharpness, 0.001f, 0.999f);
			float saw = frac < safe_sharpness ? frac / safe_sharpness
			                                  : (1.0f - frac) / (1.0f - safe_sharpness);
			mod = depth * saw;
			break;
		}
		case MarginType::Dentate:
		{
			// Symmetric triangular wave (teeth point outward)
			mod = depth * (1.0f - 2.0f * std::abs(frac - 0.5f));
			break;
		}
		case MarginType::Crenate:
		{
			// Sine wave (rounded scallops)
			mod = depth * 0.5f * (1.0f + std::sin(TWO_PI * frac));
			break;
		}
		case MarginType::Lobed:
		{
			// Low-frequency cosine with high amplitude
			mod = depth * 0.5f * (1.0f + std::cos(TWO_PI * frac));
			break;
		}
		default:
			break;
		}

		float new_r = r * (1.0f + mod);
		result.push_back(Vector2(new_r * std::cos(theta), new_r * std::sin(theta)));
	}

	return result;
}

std::vector<Vector2> LeafShapeGenerator::apply_width_balance(const std::vector<Vector2>& contour)
{
	if (width_balance == 0.0f || contour.empty())
		return contour;

	auto bb = compute_contour_bbox(contour);
	if (bb.height < 1e-10f)
		return contour;

	// Past +-1 the remap stops being monotonic and the outline folds over itself
	float balance = std::clamp(width_balance, -1.0f, 1.0f);

	std::vector<Vector2> result;
	result.reserve(contour.size());
	for (const auto& pt : contour)
	{
		float t = (pt.y() - bb.min_y) / bb.height; // 0 at base, 1 at tip
		float new_t = t + balance * t * (1.0f - t);
		result.push_back(Vector2(pt.x(), bb.min_y + new_t * bb.height));
	}
	return result;
}

Mesh LeafShapeGenerator::triangulate(const std::vector<Vector2>& contour)
{
	Mesh mesh;
	int n = static_cast<int>(contour.size());

	// Ensure counter-clockwise winding
	float signed_area = 0.0f;
	for (int i = 0; i < n; ++i)
	{
		int next = (i + 1) % n;
		signed_area += contour[i].x() * contour[next].y();
		signed_area -= contour[next].x() * contour[i].y();
	}
	bool ccw = signed_area >= 0.0f;

	// Ring 0 = boundary vertices (in CCW order)
	for (int i = 0; i < n; ++i)
	{
		int idx = ccw ? i : (n - 1 - i);
		const auto& pt = contour[idx];
		mesh.vertices.push_back(Vector3(pt.x(), pt.y(), 0.0f));
	}

	// Compute centroid
	Vector2 centroid(0.0f, 0.0f);
	for (const auto& pt : contour)
	{
		centroid += pt;
	}
	centroid /= static_cast<float>(n);

	// Number of inner rings
	int effective_resolution = std::max(contour_resolution, 8);
	int num_rings = (inner_rings >= 0) ? std::min(inner_rings, 8)
	                                   : std::clamp(effective_resolution / 16, 1, 8);

	// Generate inner ring vertices (rings 1..num_rings), each ring has n vertices
	for (int r = 1; r <= num_rings; ++r)
	{
		float t = static_cast<float>(r) / static_cast<float>(num_rings + 1);
		for (int i = 0; i < n; ++i)
		{
			int idx = ccw ? i : (n - 1 - i);
			Vector2 pt = contour[idx] * (1.0f - t) + centroid * t;
			mesh.vertices.push_back(Vector3(pt.x(), pt.y(), 0.0f));
		}
	}

	// Add centroid as final vertex
	int centroid_idx = static_cast<int>(mesh.vertices.size());
	mesh.vertices.push_back(Vector3(centroid.x(), centroid.y(), 0.0f));

	// Stitch adjacent rings with triangle strips
	for (int r = 0; r < num_rings; ++r)
	{
		int ring_offset_outer = r * n;
		int ring_offset_inner = (r + 1) * n;
		for (int i = 0; i < n; ++i)
		{
			int i_next = (i + 1) % n;
			int a = ring_offset_outer + i;
			int b = ring_offset_outer + i_next;
			int c = ring_offset_inner + i_next;
			int d = ring_offset_inner + i;

			// Two triangles per quad, stored as degenerate quads
			mesh.polygons.push_back({a, b, c, c});
			mesh.uv_loops.push_back({0, 0, 0, 0});
			mesh.polygons.push_back({a, c, d, d});
			mesh.uv_loops.push_back({0, 0, 0, 0});
		}
	}

	// Close innermost ring to centroid with a fan
	int inner_ring_offset = num_rings * n;
	for (int i = 0; i < n; ++i)
	{
		int i_next = (i + 1) % n;
		int a = inner_ring_offset + i;
		int b = inner_ring_offset + i_next;
		mesh.polygons.push_back({a, b, centroid_idx, centroid_idx});
		mesh.uv_loops.push_back({0, 0, 0, 0});
	}

	return mesh;
}

LeafShapeGenerator::BBox2D
LeafShapeGenerator::compute_contour_bbox(const std::vector<Vector2>& contour) const
{
	if (contour.empty())
	{
		return {0, 0, 0, 0, 0, 0, 0};
	}

	BBox2D bb;
	bb.min_x = contour[0].x();
	bb.max_x = contour[0].x();
	bb.min_y = contour[0].y();
	bb.max_y = contour[0].y();
	for (const auto& pt : contour)
	{
		bb.min_x = std::min(bb.min_x, pt.x());
		bb.max_x = std::max(bb.max_x, pt.x());
		bb.min_y = std::min(bb.min_y, pt.y());
		bb.max_y = std::max(bb.max_y, pt.y());
	}
	bb.width = bb.max_x - bb.min_x;
	bb.height = bb.max_y - bb.min_y;
	bb.center_x = (bb.min_x + bb.max_x) * 0.5f;
	return bb;
}

void LeafShapeGenerator::apply_venation(Mesh& mesh, const std::vector<Vector2>& contour)
{
	if (!enable_venation)
		return;

	VenationGenerator venation;
	venation.type = venation_type;
	venation.vein_density = vein_density;
	venation.kill_distance = kill_distance;
	venation.attraction_distance = attraction_distance;
	venation.growth_step_size = growth_step_size;
	venation.seed = seed;

	auto veins = venation.generate_veins(contour);
	venation.compute_vein_distances(mesh, veins);
}

void LeafShapeGenerator::apply_deformation(Mesh& mesh, const std::vector<Vector2>& contour)
{
	if (mesh.vertices.empty())
		return;

	auto bb = compute_contour_bbox(contour);
	if (bb.width < 1e-10f || bb.height < 1e-10f)
		return;

	// Compute min distance to contour edge for each vertex (for edge curl)
	std::vector<float> edge_distances(mesh.vertices.size(), std::numeric_limits<float>::max());
	for (size_t vi = 0; vi < mesh.vertices.size(); ++vi)
	{
		Vector2 pt(mesh.vertices[vi].x(), mesh.vertices[vi].y());
		for (size_t ci = 0; ci < contour.size(); ++ci)
		{
			size_t next = (ci + 1) % contour.size();
			Vector2 seg = contour[next] - contour[ci];
			float seg_len = seg.norm();
			if (seg_len < 1e-10f)
				continue;
			float t = std::clamp((pt - contour[ci]).dot(seg) / (seg_len * seg_len), 0.0f, 1.0f);
			Vector2 closest = contour[ci] + t * seg;
			float dist = (pt - closest).norm();
			edge_distances[vi] = std::min(edge_distances[vi], dist);
		}
	}

	// Apply deformations to Z coordinates
	for (size_t i = 0; i < mesh.vertices.size(); ++i)
	{
		auto& v = mesh.vertices[i];
		float nx = (v.x() - bb.center_x) / (bb.width * 0.5f); // -1 to 1 across width
		float ny = (v.y() - bb.min_y) / bb.height;            // 0 to 1 along length

		float z = 0.0f;

		// 1. Midrib curvature: circular arc bend along Y axis
		z += midrib_curvature * ny * ny * 0.5f;

		// 2. Cross curvature: parabolic cupping perpendicular to midrib
		z += cross_curvature * nx * nx * 0.3f;

		// 3. Edge curl: inward curl based on distance from edge
		float max_edge_dist = bb.width * 0.5f;
		float edge_factor =
		    1.0f - std::clamp(edge_distances[i] / (max_edge_dist * 0.3f), 0.0f, 1.0f);
		z += edge_curl * edge_factor * edge_factor * 0.2f;

		v.z() = z;
	}

	// 4. Vein displacement: raise surface along vein paths using width-aware influence
	if (vein_displacement != 0.0f)
	{
		auto it = mesh.attributes.find("vein_influence");
		if (it != mesh.attributes.end())
		{
			auto& vein_inf = static_cast<Attribute<float>&>(*it->second);
			for (size_t i = 0; i < mesh.vertices.size(); ++i)
			{
				mesh.vertices[i].z() += vein_displacement * vein_inf.data[i] * 0.5f;
			}
		}
	}
}

void LeafShapeGenerator::compute_uvs(Mesh& mesh, const std::vector<Vector2>& contour)
{
	if (contour.empty() || mesh.vertices.empty())
		return;

	auto bb = compute_contour_bbox(contour);

	// Planar UV projection: map vertex XY to 0-1 range
	mesh.uvs.resize(mesh.vertices.size());
	for (size_t i = 0; i < mesh.vertices.size(); ++i)
	{
		float u = (bb.width > 1e-10f) ? (mesh.vertices[i].x() - bb.min_x) / bb.width : 0.5f;
		float v = (bb.height > 1e-10f) ? (mesh.vertices[i].y() - bb.min_y) / bb.height : 0.5f;
		mesh.uvs[i] = Vector2(std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f));
	}

	// Set UV loops to reference the vertex UVs directly
	for (size_t i = 0; i < mesh.polygons.size(); ++i)
	{
		mesh.uv_loops[i] = mesh.polygons[i]; // UV indices = vertex indices for planar projection
	}
}

Mesh LeafShapeGenerator::generate()
{
	// Pipeline
	size_t base_index = 0;
	std::vector<Vector2> contour = sample_contour(base_index);
	contour = apply_width_balance(contour);
	contour = apply_margin(contour);
	Mesh mesh = triangulate(contour);
	compute_uvs(mesh, contour);
	apply_venation(mesh, contour);
	apply_deformation(mesh, contour);

	// Origin at the leaf base (petiole attachment), leaf extending along +Y like the fallback quad leaf.
	// Width balance and margin move points one-to-one, so the base sample keeps its index: it is the notch when there is one.
	Vector2 base_point = contour[base_index];
	for (auto& v : mesh.vertices)
	{
		v.x() -= base_point.x();
		v.y() -= base_point.y();
	}

	return mesh;
}

} // namespace Mtree
