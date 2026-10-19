#pragma once
#include "source/tree/Node.hpp"
#include "source/utilities/GeometryUtilities.hpp"
#include <vector>

namespace Mtree
{
using Vector3 = Eigen::Vector3f;

// A closed mesh turned into an inside/outside grid of cells, used to keep branches inside it
class CrownHull
{
  public:
	CrownHull(const std::vector<float>& vertices, const std::vector<int>& triangles,
	          int cells_on_longest_side);

	bool contains(const Vector3& point) const;

	// how far a straight branch can go from start before it leaves the hull
	float length_inside(const Vector3& start, const Vector3& direction, float max_length) const;

	// the last step before meshing: saws off everything outside the hull and caps the cuts,
	// leaving every other node exactly as the tree functions made it
	void prune_tree(std::vector<Stem>& stems) const;

  private:
	Vector3 bounds_min{0, 0, 0};
	float cell_size = 1;
	int cells_x = 0;
	int cells_y = 0;
	int cells_z = 0;
	std::vector<bool> inside;

	int cell_index(int x, int y, int z) const { return (z * cells_y + y) * cells_x + x; }

	void prune_node_rec(Node& node, const Vector3& node_position, bool below_crown) const;
	void saw_off(Node& node, const Vector3& node_position, const Vector3& node_end) const;
	float find_cut_fraction(const Vector3& start, const Vector3& end) const;
};

} // namespace Mtree
