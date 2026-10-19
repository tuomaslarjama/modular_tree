from __future__ import annotations

from random import randint

import bpy

from ...m_tree_wrapper import lazy_m_tree
from ...viewport.shape_formulas import BLENDER_SHAPE_MAP
from ...viewport.shape_formulas import CrownShape as PyCrownShape
from ..base_types.node import MtreeFunctionNode
from ..debounce import schedule_build
from .crown_hull import build_crown_hull

# Parameter groupings for organized UI
BASIC_PARAMS = ["seed", "start", "end", "length", "branches_density", "start_angle"]
SHAPE_PARAMS = ["randomness", "flatness", "up_attraction", "gravity_strength", "stiffness"]
SPLIT_PARAMS = ["split_proba", "split_radius", "split_angle"]
ADVANCED_PARAMS = ["break_chance", "resolution", "start_radius"]

# Parameter descriptions for tooltips
PARAM_DESCRIPTIONS = {
    "seed": "Random seed for reproducible results",
    "start": "Position along parent where branches start (0=base, 1=tip)",
    "end": "Position along parent where branches end (0=base, 1=tip)",
    "length": "Maximum length of branches",
    "branches_density": "Number of branches per unit length",
    "start_angle": "Angle between branch and parent at the base",
    "randomness": "Amount of random variation in branch direction",
    "flatness": "Horizontal spread (0=spherical, 1=flat canopy)",
    "up_attraction": "Tendency to grow upward (negative values droop)",
    "gravity_strength": "How much branches bend under their weight",
    "stiffness": "Resistance to bending from gravity",
    "split_proba": "Probability of a branch splitting into two",
    "split_radius": "Minimum radius for splits to occur",
    "split_angle": "Angle between split branches",
    "phillotaxis": "Spiral angle between branch nodes (Spiral arrangement only)",
    "phillotaxis_offset": "Rolls the whole branch pattern around the parent (degrees)",
    "offset_from_up": "Measure the offset from the parent's upper side (with Alternate, 90 = branches only sideways)",
    "skip_chance": "Chance that a single branch at a node is left out, so pairs and whorls break up",
    "angle_variation": "Height-based angle variation: positive = upward at top, downward at base",
    "break_chance": "Probability of a branch breaking/stopping",
    "resolution": "Number of segments per unit length",
    "start_radius": "Branch radius at base relative to parent",
}


from bpy.app.handlers import persistent

# Sockets added after the first templates were made. Kept out of init() so older
# saved nodes can be given the same sockets when a file loads.
LATER_SOCKETS = [
    ("mt_FloatSocket", "Phillotaxis Offset", "phillotaxis_offset", 0.0, {"min_value": 0, "max_value": 360}),
    ("mt_BoolSocket", "Offset From Up", "offset_from_up", False, {}),
    ("mt_FloatSocket", "Skip Chance", "skip_chance", 0.0, {"min_value": 0, "max_value": 1}),
    ("mt_FloatSocket", "Angle Spread", "angle_variation", 0.0, {"min_value": -45, "max_value": 45}),
]

def _update_crown_property(self, context):
    """Trigger auto-update when crown shape properties change."""
    mesher = self.get_mesher()
    if mesher is not None:
        schedule_build(mesher)


class BranchNode(bpy.types.Node, MtreeFunctionNode):
    bl_idname = "mt_BranchNode"
    bl_label = "Branches"

    # Collapsible section states for inspector panel
    show_basic: bpy.props.BoolProperty(name="Basic", default=True)
    show_shape: bpy.props.BoolProperty(name="Shape", default=True)
    show_split: bpy.props.BoolProperty(name="Splitting", default=False)
    show_crown: bpy.props.BoolProperty(name="Crown Shape", default=False)
    show_advanced: bpy.props.BoolProperty(name="Advanced", default=False)

    # Crown shape enum
    crown_shape: bpy.props.EnumProperty(
        name="Crown Shape",
        items=[
            ("CYLINDRICAL", "Cylindrical", "Uniform branch length (default)"),
            ("CONICAL", "Conical", "Longer at top (pine/fir)"),
            ("SPHERICAL", "Spherical", "Round crown (oak/maple)"),
            ("HEMISPHERICAL", "Hemispherical", "Dome-shaped"),
            ("TAPERED_CYLINDRICAL", "Tapered Cylindrical", "Gradual taper"),
            ("FLAME", "Flame", "Flame-shaped (cedar)"),
            ("INVERSE_CONICAL", "Inverse Conical", "Wider at bottom"),
            ("TEND_FLAME", "Tend Flame", "Soft flame shape"),
        ],
        default="CYLINDRICAL",
        description="Crown shape envelope that controls branch length based on height",
        update=_update_crown_property,
    )

    arrangement: bpy.props.EnumProperty(
        name="Arrangement",
        items=[
            ("SPIRAL", "Spiral", "One branch per node, turned by the Phillotaxis angle (birch, oak, rowan)"),
            ("ALTERNATE", "Alternate", "One branch per node in two rows, 180 degrees apart (linden and elm shoots)"),
            ("OPPOSITE", "Opposite", "Pairs, each pair turned 90 degrees from the last (maple, ash)"),
            ("WHORLED", "Whorled", "Rings of branches around the parent (pine, spruce)"),
        ],
        default="SPIRAL",
        update=_update_crown_property,
    )

    crown_hull: bpy.props.PointerProperty(
        name="Crown Hull",
        type=bpy.types.Object,
        poll=lambda self, candidate: candidate.type == "MESH",
        description="Closed mesh the branches have to stay inside",
        update=_update_crown_property,
    )

    whorl_count: bpy.props.IntProperty(
        name="Whorl Count", default=4, min=3, max=8, update=_update_crown_property
    )

    skip_chance: bpy.props.FloatProperty(
        name="Skip Chance",
        default=0.0,
        min=0.0,
        max=1.0,
        description="Chance that a single branch at a node is left out, so pairs and whorls break up",
        update=_update_crown_property,
    )

    angle_variation: bpy.props.FloatProperty(
        name="Angle Spread",
        default=0.0,
        min=-45.0,
        max=45.0,
        description="Height-based angle variation: positive = upward at top, downward at base",
        update=_update_crown_property,
    )

    show_crown_preview: bpy.props.BoolProperty(
        name="Show Preview",
        default=False,
        description="Show crown shape envelope in 3D viewport",
    )

    @property
    def tree_function(self):
        return lazy_m_tree.BranchFunction

    def draw(self, context, layout):
        layout.prop(self, "crown_shape", text="Crown")
        layout.prop(self, "arrangement", text="") 

    # Mapping from socket property_name to nested struct path
    NESTED_PROPERTY_MAP = {
        # Distribution params
        "start": ("distribution", "start"),
        "end": ("distribution", "end"),
        "branches_density": ("distribution", "density"),
        "phillotaxis": ("distribution", "phillotaxis"),
        "phillotaxis_offset": ("distribution", "phillotaxis_offset"),
        "offset_from_up": ("distribution", "offset_from_up"),
        "skip_chance": ("distribution", "skip_chance"),
        # Crown params
        "angle_variation": ("crown", "angle_variation"),
        # Gravity params
        "gravity_strength": ("gravity", "strength"),
        "stiffness": ("gravity", "stiffness"),
        "up_attraction": ("gravity", "up_attraction"),
        # Split params
        "split_proba": ("split", "probability"),
        "split_radius": ("split", "radius"),
        "split_angle": ("split", "angle"),
    }

    def construct_function(self):
        func = self.tree_function()

        # Handle exposed_parameters (from base class pattern)
        for parameter in self.exposed_parameters:
            setattr(func, parameter, getattr(self, parameter))

        # Handle input sockets with nested property mapping
        for input_socket in self.inputs:
            if not input_socket.is_property:
                continue

            prop_name = input_socket.property_name
            if input_socket.bl_idname == "mt_PropertySocket":
                value = input_socket.get_property()
            else:
                value = input_socket.property_value

            # Check if this is a nested property
            if prop_name in self.NESTED_PROPERTY_MAP:
                struct_name, field_name = self.NESTED_PROPERTY_MAP[prop_name]
                struct = getattr(func, struct_name)
                setattr(struct, field_name, value)
            else:
                setattr(func, prop_name, value)

        # Map Blender enum to C++ enum for crown shape
        # Uses shared BLENDER_SHAPE_MAP and converts via int (both enums share same values)
        py_shape = BLENDER_SHAPE_MAP.get(self.crown_shape, PyCrownShape.Cylindrical)
        func.crown.shape = lazy_m_tree.CrownShape(int(py_shape))

        if self.arrangement == "ALTERNATE":
            func.distribution.phillotaxis = 180.0
        elif self.arrangement == "OPPOSITE":
            func.distribution.branches_per_node = 2
            func.distribution.phillotaxis = 90.0
        elif self.arrangement == "WHORLED":
            func.distribution.branches_per_node = self.whorl_count
            func.distribution.phillotaxis = 180.0 / self.whorl_count

        hull = build_crown_hull(self, self.crown_hull)
        if hull is not None:
            func.hull = hull

        # Handle child nodes
        for child in self.get_child_nodes():
            from ..base_types.node import MtreeFunctionNode

            if isinstance(child, MtreeFunctionNode):
                child_function = child.construct_function()
                func.add_child(child_function)

        return func

    def init(self, context):
        self.add_input("mt_TreeSocket", "Tree", is_property=False)

        # Basic parameters
        self.add_input(
            "mt_IntSocket",
            "Seed",
            min_value=0,
            property_name="seed",
            property_value=randint(0, 1000),
            description=PARAM_DESCRIPTIONS["seed"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Start",
            min_value=0,
            max_value=1,
            property_name="start",
            property_value=0.1,
            description=PARAM_DESCRIPTIONS["start"],
        )
        self.add_input(
            "mt_FloatSocket",
            "End",
            min_value=0,
            max_value=1,
            property_name="end",
            property_value=0.95,
            description=PARAM_DESCRIPTIONS["end"],
        )
        self.add_input(
            "mt_PropertySocket",
            "Length",
            min_value=0,
            property_name="length",
            property_value=9,
            description=PARAM_DESCRIPTIONS["length"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Density",
            min_value=0.0001,
            property_name="branches_density",
            property_value=1,
            description=PARAM_DESCRIPTIONS["branches_density"],
        )
        self.add_input(
            "mt_PropertySocket",
            "Start Angle",
            min_value=0,
            max_value=180,
            property_name="start_angle",
            property_value=45,
            description=PARAM_DESCRIPTIONS["start_angle"],
        )

        # Shape parameters
        self.add_input(
            "mt_PropertySocket",
            "Randomness",
            min_value=0.0001,
            property_name="randomness",
            property_value=0.5,
            description=PARAM_DESCRIPTIONS["randomness"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Flatness",
            min_value=0,
            max_value=1,
            property_name="flatness",
            property_value=0.2,
            description=PARAM_DESCRIPTIONS["flatness"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Up Attraction",
            property_name="up_attraction",
            property_value=0.25,
            description=PARAM_DESCRIPTIONS["up_attraction"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Gravity",
            property_name="gravity_strength",
            property_value=10,
            description=PARAM_DESCRIPTIONS["gravity_strength"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Stiffness",
            property_name="stiffness",
            property_value=0.1,
            description=PARAM_DESCRIPTIONS["stiffness"],
        )

        # Split parameters
        self.add_input(
            "mt_FloatSocket",
            "Split Chance",
            min_value=0,
            max_value=1,
            property_name="split_proba",
            property_value=0.5,
            description=PARAM_DESCRIPTIONS["split_proba"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Split Radius",
            min_value=0.0001,
            property_name="split_radius",
            property_value=0.8,
            description=PARAM_DESCRIPTIONS["split_radius"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Split Angle",
            min_value=0,
            max_value=180,
            property_name="split_angle",
            property_value=35.0,
            description=PARAM_DESCRIPTIONS["split_angle"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Phillotaxis",
            min_value=0,
            max_value=360,
            property_name="phillotaxis",
            property_value=137.5,
            description=PARAM_DESCRIPTIONS["phillotaxis"],
        )
        
        # Advanced parameters
        self.add_input(
            "mt_FloatSocket",
            "Break Chance",
            min_value=0,
            property_name="break_chance",
            property_value=0.02,
            description=PARAM_DESCRIPTIONS["break_chance"],
        )
        self.add_input(
            "mt_FloatSocket",
            "Resolution",
            min_value=0.0001,
            property_name="resolution",
            property_value=3,
            description=PARAM_DESCRIPTIONS["resolution"],
        )
        self.add_input(
            "mt_PropertySocket",
            "Start Radius",
            min_value=0.0001,
            property_name="start_radius",
            property_value=0.4,
            description=PARAM_DESCRIPTIONS["start_radius"],
        )

        self.add_missing_sockets()

        self.add_output("mt_TreeSocket", "Tree", is_property=False)

    def _get_socket_by_property(self, property_name: str):
        """Find input socket by property_name attribute."""
        for socket in self.inputs:
            if hasattr(socket, "property_name") and socket.property_name == property_name:
                return socket
        return None

    def apply_preset(self, preset_name: str):
        """Apply a preset by setting socket property values."""
        from ...presets import TREE_PRESETS, _generate_random_params

        if preset_name == "RANDOM":
            params = _generate_random_params()
        else:
            preset = TREE_PRESETS.get(preset_name)
            if not preset:
                return
            params = preset.branches

        for param_name, param_value in params.items():
            socket = self._get_socket_by_property(param_name)
            if socket:
                socket.property_value = float(param_value)

    def _draw_socket(self, layout, param: str) -> None:
        socket = self._get_socket_by_property(param)
        if socket and socket.is_property:
            col = layout.column()
            socket.draw(bpy.context, col, self, socket.name)
            if param in PARAM_DESCRIPTIONS:
                sub = col.row()
                sub.scale_y = 0.6
                sub.label(text=f"  {PARAM_DESCRIPTIONS[param]}")

    def _draw_section(self, layout, title: str, show_prop: str, params: list) -> None:
        """Draw a collapsible section with parameters."""
        box = layout.box()
        row = box.row()
        show = getattr(self, show_prop)
        row.prop(
            self,
            show_prop,
            icon="TRIA_DOWN" if show else "TRIA_RIGHT",
            icon_only=True,
            emboss=False,
        )
        row.label(text=title)

        if show:
            for param in params:
                self._draw_socket(box, param)

    def draw_inspector(self, context, layout):
        """Draw organized parameters in the Properties panel (N key)."""
        # Preset buttons
        box = layout.box()
        box.label(text="Presets", icon="PRESET")
        row = box.row(align=True)
        for preset_name in ["OAK", "PINE", "WILLOW", "RANDOM"]:
            op = row.operator("mtree.apply_branch_node_preset", text=preset_name.capitalize())
            op.preset = preset_name
            op.node_tree_name = self.id_data.name
            op.node_name = self.name
        
        box.prop(self, "crown_hull")

        # Existing sections
        self._draw_section(layout, "Basic", "show_basic", BASIC_PARAMS)
        self._draw_section(layout, "Shape", "show_shape", SHAPE_PARAMS)
        self._draw_section(layout, "Splitting", "show_split", SPLIT_PARAMS)

        box = layout.box()
        box.label(text="Arrangement")
        box.prop(self, "arrangement", text="")
        if self.arrangement == "SPIRAL":
            self._draw_socket(box, "phillotaxis")
        if self.arrangement == "WHORLED":
            box.prop(self, "whorl_count")
        self._draw_socket(box, "phillotaxis_offset")
        self._draw_socket(box, "offset_from_up")
        self._draw_socket(box, "skip_chance")

        # Crown shape section with enum dropdown
        box = layout.box()
        row = box.row()
        show = self.show_crown
        row.prop(
            self,
            "show_crown",
            icon="TRIA_DOWN" if show else "TRIA_RIGHT",
            icon_only=True,
            emboss=False,
        )
        row.label(text="Crown Shape")

        if show:
            box.prop(self, "crown_shape", text="")
            box.prop(self, "show_crown_preview", text="Preview in Viewport")
            self._draw_socket(box, "angle_variation")
            box.label(text="Controls branch length and angle based on height", icon="INFO")

        self._draw_section(layout, "Advanced", "show_advanced", ADVANCED_PARAMS)

    def add_missing_sockets(self):
        existing = {socket.property_name for socket in self.inputs if socket.is_property}
        for socket_type, label, property_name, default, limits in LATER_SOCKETS:
            if property_name in existing:
                continue
            # an older node may still hold this value as a node property
            value = getattr(self, property_name, default)
            self.add_input(
                socket_type,
                label,
                property_name=property_name,
                property_value=value,
                description=PARAM_DESCRIPTIONS.get(property_name, ""),
                **limits,
            )
            
@persistent
def add_missing_sockets_on_load(_):
    for node_tree in bpy.data.node_groups:
        if node_tree.bl_idname != "mt_MtreeNodeTree":
            continue
        for node in node_tree.nodes:
            if node.bl_idname == "mt_BranchNode":
                node.add_missing_sockets()