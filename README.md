# Modular Tree (Larjama fork)

[![Build and Release](https://github.com/tuomaslarjama/modular_tree/actions/workflows/CD.yml/badge.svg)](https://github.com/tuomaslarjama/modular_tree/actions/workflows/CD.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

A fork of the procedural tree add-on for Blender, extended for real street trees. It adds botanical branch arrangements, mesh hulls that shape and prune a crown, cordate leaf shapes and a Pivot Painter 2.0 export that works with Unreal Engine 5's hierarchical wind.

I built it for [GISFoliage](https://tldev.name/gisfoliage), a pipeline that turns the City of Turku's tree registry and LiDAR heights into placed, wind-animated trees in Unreal Engine 5. The 14 tree and shrub templates there are node trees made with this fork.

> Based on [GoodPie's maintained fork](https://github.com/GoodPie/modular_tree) (v5.5.2) of [Mtree by MaximeHerpin](https://github.com/MaximeHerpin/modular_tree). Everything not listed below works as in upstream, and its [documentation](#upstream-documentation) still applies.

## What this fork adds

### Branch arrangement (Branches node)

| Arrangement | Branches per node | Typical species |
|---|---|---|
| **Spiral** | one, turned by the Phyllotaxis angle | birch, oak, rowan |
| **Alternate** | one, in two rows 180° apart | linden and elm shoots |
| **Opposite** | pairs, each pair turned 90° from the last | maple, ash |
| **Whorled** | rings of 3–8 (Whorl Count) | pine, spruce |

Further controls on the same node:

- **Phyllotaxis Offset** rolls the whole branch pattern around the parent.
- **Offset From Up** measures that offset from the parent's upper side. With Alternate and a 90° offset, branches grow only sideways.
- **Skip Chance** leaves out single branches at a node, so pairs and whorls break up the way they do on real trees.
- **Angle Spread** varies the branch angle with height: positive values point branches upward at the top and downward at the base.

### Crown hulls

A hull is any closed mesh object in the scene. It is converted into an inside/outside grid of 64 cells along its longest side, about 16 cm cells for a 10 m crown.

- **Shaping hull** (*Crown Hull* on the Branches and Growth nodes): branches stay inside the hull while they grow, and Growth shoots that would leave it are pruned.
- **Pruning hull** (*Prune Hull* on the Tree Mesher): the last step before meshing. It saws off everything outside the hull and caps the cut ends, leaving the rest of the tree exactly as the tree functions made it: for street trees cut back from a building or a road.
- The tree rebuilds when a hull mesh is edited or moved, so a crown can be shaped live by editing the hull.

### Growth vigor (Growth node, prototype)

- **Vigor** scales the energy the tree distributes each year. 1 leaves growth unchanged.
- **Vigor Sets Shoot Length** gives strong shoots longer segments and weak shoots shorter ones.

### Leaf shapes (Leaf Shape node)

New options on top of the superformula outline, enough for cordate leaves such as linden:

- **Width Balance** moves the widest point toward the tip or the base.
- **Base Notch** (depth, width) and **Base Asymmetry** for heart-shaped and uneven leaf bases.
- **Tip** (length, width) for drawn-out leaf tips.
- **Inner Rings** sets the mesh rings inside the outline (automatic by default).
- The leaf mesh origin is at the leaf base (petiole attachment), as with the fallback quad leaf, so leaves attach and rotate at the right point.

### Mesh attributes

- **`distance_to_tip`**: the distance from each vertex to the end of its branch. The Leaves node group uses it for a **Terminal Growth Only** option (with *Terminal Length*), which places leaves only near branch tips.
- **`parent_stem_id`**: the stem each stem grows from, used by the Pivot Painter export below.

### Pivot Painter 2.0 export for Unreal Engine 5

- The alpha channel of `PivotPos_Index` now holds each stem's **parent index**, packed the way Epic's `ms_PivotPainter2_UnpackIntegerAsFloat` expects. Upstream wrote the hierarchy depth there, which Epic's `PivotPainter2FoliageShader` cannot rebuild the branch hierarchy from.
- Pivot positions and extents are written in **Unreal units (cm)** instead of Blender metres.
- Texture RGB is no longer scaled by alpha. Blender's default alpha mode premultiplied it, which shifted the packed positions.
- The **Export** button shows up again.

See the [UE5 setup guide](python_classes/pivot_painter/UE5_MATERIAL_SETUP.md) for import settings.

### Fixes

- Building from source works again with scikit-build-core 0.10.
- Very short branches (for example from ramp-driven lengths) no longer produce NaN vertices, which showed up as stretched faces after FBX export.
- Blender 5.0: socket values are clamped without writing to the raw ID property, and reinstalling the add-on with a file open no longer crashes.
- Growth: the Grow, Cut and Split thresholds and Lateral Activation accept values up to 10, and Apical Dominance up to 2.

## Backward compatibility

Every new option is off by default, so node trees saved with the upstream add-on load and grow the same branches. Two deliberate changes do affect existing files: generated leaves now attach at their base instead of their tip, and a branch too short to form a segment is dropped instead of producing NaN vertices.

Sockets added later (Phyllotaxis Offset, Offset From Up, Skip Chance, Angle Spread) are added to older saved nodes when a file loads. Skip Chance draws random numbers only when it is above 0, so existing seeds give identical trees.

## Installation

1. Download the zip for your platform from [Releases](https://github.com/tuomaslarjama/modular_tree/releases).
2. In Blender (4.3 or later): **Edit > Preferences > Get Extensions**, open the **⌄** menu at the top right and choose **Install from Disk**.
3. Select the zip.

Disable or uninstall the upstream Modular Tree first. Both add-ons register the same node types, so only one can be enabled at a time. The fork has its own add-on id (`modular_tree_larjama`), so an upstream update can't replace it.

## Building from source

The build is the same as upstream: see [CONTRIBUTING.md](CONTRIBUTING.md). Each push to this repository builds the Windows, macOS and Linux packages in GitHub Actions and publishes them as a release.

If CMake picks up a different Python than the one your Blender ships with (3.11 for Blender 4.3–5.0, 3.13 for 5.1), pass it explicitly with `-DPython_EXECUTABLE=<path to python>`.

## Upstream documentation

- [Crown Shape Guide](docs/crown_shape_guide.md)
- [Growth Node Reference](docs/growth_node_reference.md)
- [Leaf Generation Guide](docs/leaf_generation.md)

## License

- Blender add-on: [GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html)
- Core library: [MIT](https://choosealicense.com/licenses/mit/)

## Credits

- **Original author:** [MaximeHerpin](https://github.com/MaximeHerpin)
- **Maintained fork:** [GoodPie](https://github.com/GoodPie) (Brandyn Britton)
- **This fork:** [Tuomas Larjama](https://tldev.name)

This fork was built with AI-assisted development: I designed each feature and set its target, Claude (Anthropic) wrote first drafts of much of the code, and I reviewed, tested and integrated every change.
