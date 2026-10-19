import bpy
from bpy.app.handlers import persistent
from mathutils import Matrix

from ...m_tree_wrapper import lazy_m_tree
from ..debounce import schedule_build

# cells along the hull's longest side; a 10 m crown gets about 16 cm cells
HULL_CELLS = 64


def build_crown_hull(node, hull_object):
    """Turn a mesh object into a CrownHull in the tree object's space, or None."""
    if hull_object is None or hull_object.type != "MESH":
        return None

    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = hull_object.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        to_tree_space = _tree_matrix(node).inverted() @ evaluated.matrix_world
        vertices = []
        for vertex in mesh.vertices:
            position = to_tree_space @ vertex.co
            vertices.extend((position.x, position.y, position.z))
        triangles = []
        for triangle in mesh.loop_triangles:
            triangles.extend(triangle.vertices)
    finally:
        evaluated.to_mesh_clear()

    if not triangles:
        return None
    return lazy_m_tree.CrownHull(vertices, triangles, HULL_CELLS)


def _tree_matrix(node):
    # the tree is generated in its object's local space, so the hull has to be moved there
    mesher = node.get_mesher()
    tree_object = bpy.context.scene.objects.get(mesher.tree_object) if mesher else None
    if tree_object is None:
        return Matrix.Identity(4)
    return tree_object.matrix_world


@persistent
def rebuild_trees_on_hull_change(scene, depsgraph):
    changed_objects = set()
    moved_objects = set()
    for update in depsgraph.updates:
        if not isinstance(update.id, bpy.types.Object):
            continue
        name = update.id.original.name
        if update.is_updated_geometry or update.is_updated_transform:
            changed_objects.add(name)
        if update.is_updated_transform:
            moved_objects.add(name)
    if not changed_objects:
        return

    for node_tree in bpy.data.node_groups:
        if node_tree.bl_idname != "mt_MtreeNodeTree":
            continue
        for node in node_tree.nodes:
            hull_objects = [getattr(node, name, None) for name in ("crown_hull", "prune_hull")]
            hull_objects = [hull for hull in hull_objects if hull is not None]
            if not hull_objects:
                continue
            mesher = node.get_mesher()
            if mesher is None:
                continue
            for hull_object in hull_objects:
                # the tree as its own hull would rebuild itself forever
                if hull_object.name == mesher.tree_object:
                    continue
                if hull_object.name in changed_objects or mesher.tree_object in moved_objects:
                    schedule_build(mesher)
                    break
