#include "CrownHull.hpp"
#include <algorithm>
#include <cmath>

namespace Mtree
{
CrownHull::CrownHull(const std::vector<float>& vertices, const std::vector<int>& triangles,
                     int cells_on_longest_side)
{
	int vertex_count = vertices.size() / 3;
	if (vertex_count == 0 || triangles.size() < 3)
		return;

	std::vector<Vector3> points(vertex_count);
	for (int i = 0; i < vertex_count; i++)
		points[i] = Vector3{vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]};

	bounds_min = points[0];
	Vector3 bounds_max = points[0];
	for (const Vector3& point : points)
	{
		bounds_min = bounds_min.cwiseMin(point);
		bounds_max = bounds_max.cwiseMax(point);
	}

	Vector3 size = bounds_max - bounds_min;
	cell_size = std::max(size.maxCoeff() / std::max(cells_on_longest_side, 1), 1e-4f);
	cells_x = std::max(1, (int)std::ceil(size.x() / cell_size));
	cells_y = std::max(1, (int)std::ceil(size.y() / cell_size));
	cells_z = std::max(1, (int)std::ceil(size.z() / cell_size));
	inside.assign(cells_x * cells_y * cells_z, false);

	// Each column of cells gets one vertical ray. The heights where it crosses the surface
	// come in pairs: entering the mesh, leaving it. The ray is nudged off the cell center so it
	// doesn't run exactly through the diagonal edges of a triangulated grid-aligned mesh,
	// which would count one crossing twice.
	float nudge_x = cell_size * 0.0137f;
	float nudge_y = cell_size * 0.0071f;
	std::vector<std::vector<float>> crossings(cells_x * cells_y);

	for (size_t t = 0; t + 2 < triangles.size(); t += 3)
	{
		const Vector3& a = points[triangles[t]];
		const Vector3& b = points[triangles[t + 1]];
		const Vector3& c = points[triangles[t + 2]];

		float area = (b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y());
		if (std::abs(area) < 1e-12f) // a vertical triangle is never crossed by a vertical ray
			continue;

		float min_x = std::min({a.x(), b.x(), c.x()}) - bounds_min.x();
		float max_x = std::max({a.x(), b.x(), c.x()}) - bounds_min.x();
		float min_y = std::min({a.y(), b.y(), c.y()}) - bounds_min.y();
		float max_y = std::max({a.y(), b.y(), c.y()}) - bounds_min.y();
		int first_x = std::max(0, (int)std::floor(min_x / cell_size) - 1);
		int last_x = std::min(cells_x - 1, (int)std::ceil(max_x / cell_size) + 1);
		int first_y = std::max(0, (int)std::floor(min_y / cell_size) - 1);
		int last_y = std::min(cells_y - 1, (int)std::ceil(max_y / cell_size) + 1);

		for (int y = first_y; y <= last_y; y++)
		{
			for (int x = first_x; x <= last_x; x++)
			{
				float px = bounds_min.x() + (x + 0.5f) * cell_size + nudge_x;
				float py = bounds_min.y() + (y + 0.5f) * cell_size + nudge_y;
				float weight_a =
				    ((b.x() - px) * (c.y() - py) - (c.x() - px) * (b.y() - py)) / area;
				float weight_b =
				    ((c.x() - px) * (a.y() - py) - (a.x() - px) * (c.y() - py)) / area;
				float weight_c = 1 - weight_a - weight_b;
				if (weight_a < 0 || weight_b < 0 || weight_c < 0)
					continue;
				float height = weight_a * a.z() + weight_b * b.z() + weight_c * c.z();
				crossings[y * cells_x + x].push_back(height);
			}
		}
	}

	for (int y = 0; y < cells_y; y++)
	{
		for (int x = 0; x < cells_x; x++)
		{
			auto& heights = crossings[y * cells_x + x];
			std::sort(heights.begin(), heights.end());
			// an odd count means a hole in the mesh; the last unpaired crossing is ignored
			for (size_t i = 0; i + 1 < heights.size(); i += 2)
			{
				int first_z = std::max(
				    0, (int)std::ceil((heights[i] - bounds_min.z()) / cell_size - 0.5f));
				int last_z = std::min(
				    cells_z - 1, (int)std::floor((heights[i + 1] - bounds_min.z()) / cell_size - 0.5f));
				for (int z = first_z; z <= last_z; z++)
					inside[cell_index(x, y, z)] = true;
			}
		}
	}
}

bool CrownHull::contains(const Vector3& point) const
{
	if (inside.empty()) // no usable mesh, so nothing is constrained
		return true;

	Vector3 cell = (point - bounds_min) / cell_size;
	int x = (int)std::floor(cell.x());
	int y = (int)std::floor(cell.y());
	int z = (int)std::floor(cell.z());
	if (x < 0 || y < 0 || z < 0 || x >= cells_x || y >= cells_y || z >= cells_z)
		return false;
	return inside[cell_index(x, y, z)];
}

float CrownHull::length_inside(const Vector3& start, const Vector3& direction,
                               float max_length) const
{
	if (!contains(start))
		return 0;

	float step = cell_size * 0.5f;
	for (float distance = step; distance < max_length; distance += step)
	{
		if (!contains(start + direction * distance))
			return distance - step;
	}
	return max_length;
}

namespace
{
constexpr int kCapCreatorId = -1;      // marks cap nodes, so nothing grows on them or cuts them
constexpr float kCapLengthFactor = 0.5f; // cap length relative to the cut radius; lower = flatter

// A sawn branch would otherwise end in an open tube. The cap starts at the radius the branch
// had at the cut and narrows to a point over half that radius.
std::shared_ptr<NodeChild> make_cap(const Node& cut_node, float cut_radius)
{
	float cap_length = std::max(cut_radius * kCapLengthFactor, 0.0005f);
	Node cap_tip{cut_node.direction, cut_node.tangent, cap_length * 0.1f, 0.00001f, kCapCreatorId};
	Node cap{cut_node.direction, cut_node.tangent, cap_length, cut_radius, kCapCreatorId};
	cap.children.push_back(std::make_shared<NodeChild>(NodeChild{std::move(cap_tip), 1}));
	return std::make_shared<NodeChild>(NodeChild{std::move(cap), 1});
}
} // namespace

void CrownHull::prune_tree(std::vector<Stem>& stems) const
{
	if (inside.empty())
		return;
	for (Stem& stem : stems)
		prune_node_rec(stem.node, stem.position, !contains(stem.position));
}

// below_crown is true for the trunk while it has not yet reached the hull: that part is kept
// as it is, since a crown hull rarely reaches down to the ground
void CrownHull::prune_node_rec(Node& node, const Vector3& node_position, bool below_crown) const
{
	Vector3 node_end = node_position + node.direction * node.length;

	if (!below_crown && !contains(node_end))
		saw_off(node, node_position, node_end);

	// side branches that start outside the hull are removed whole
	for (size_t i = 1; i < node.children.size(); i++)
	{
		Vector3 child_position =
		    node_position + node.direction * node.length * node.children[i]->position_in_parent;
		if (!contains(child_position))
		{
			node.children.erase(node.children.begin() + i);
			i--;
		}
	}

	for (size_t i = 0; i < node.children.size(); i++)
	{
		auto& child = node.children[i];
		if (child->node.creator_id == kCapCreatorId)
			continue;
		Vector3 child_position =
		    node_position + node.direction * node.length * child->position_in_parent;
		bool child_below_crown = i == 0 && below_crown && !contains(child_position);
		prune_node_rec(child->node, child_position, child_below_crown);
	}
}

void CrownHull::saw_off(Node& node, const Vector3& node_position, const Vector3& node_end) const
{
	float cut_fraction = find_cut_fraction(node_position, node_end);

	// the mesher blends a node's radius toward its continuation's radius, so this is the
	// radius the branch had at the cut before anything was removed
	float end_radius = node.children.empty() ? node.radius : node.children[0]->node.radius;
	float cut_radius = node.radius + (end_radius - node.radius) * cut_fraction;

	float old_length = node.length;
	node.length *= cut_fraction;

	// the continuation starts past the cut, so it goes; side branches that start before the
	// cut stay at the same point in space on the shorter node
	std::vector<std::shared_ptr<NodeChild>> kept;
	kept.push_back(make_cap(node, cut_radius));
	for (size_t i = 1; i < node.children.size(); i++)
	{
		auto& child = node.children[i];
		if (child->position_in_parent < cut_fraction)
		{
			child->position_in_parent *= old_length / node.length;
			kept.push_back(child);
		}
	}
	node.children = std::move(kept);
}

float CrownHull::find_cut_fraction(const Vector3& start, const Vector3& end) const
{
	// start is inside, end is outside: halve the gap until it is a few millimeters wide
	float inside_fraction = 0;
	float outside_fraction = 1;
	for (int i = 0; i < 10; i++)
	{
		float middle = (inside_fraction + outside_fraction) / 2;
		if (contains(start + (end - start) * middle))
			inside_fraction = middle;
		else
			outside_fraction = middle;
	}
	return std::max(inside_fraction, 0.01f);
}

} // namespace Mtree
