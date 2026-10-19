import bpy
import nodeitems_utils
from bpy.utils import register_class, unregister_class

from . import (
    base_types,
    debounce,
    export_nodes,
    node_categories,
    properties,
    sockets,
    tree_function_nodes,
)
from .base_types import node_tree
from .tree_function_nodes import branch_node, crown_hull


def register():
    node_tree.addon_ready = False
    base_types.register()
    sockets.register()
    tree_function_nodes.register()
    export_nodes.register()
    properties.register()
    node_categories.register()
    debounce.register()
    bpy.app.handlers.load_post.append(branch_node.add_missing_sockets_on_load)
    bpy.app.handlers.depsgraph_update_post.append(crown_hull.rebuild_trees_on_hull_change)
    node_tree.addon_ready = True


def unregister():
    node_tree.addon_ready = False
    if branch_node.add_missing_sockets_on_load in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(branch_node.add_missing_sockets_on_load)
    if crown_hull.rebuild_trees_on_hull_change in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(crown_hull.rebuild_trees_on_hull_change)
    debounce.unregister()
    node_categories.unregister()
    base_types.unregister()
    sockets.unregister()
    properties.unregister()
    export_nodes.unregister()
    tree_function_nodes.unregister()
