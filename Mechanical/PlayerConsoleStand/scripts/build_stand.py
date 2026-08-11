import argparse
import json
import math
from pathlib import Path
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from stand_parameters import (  # noqa: E402
    StandParameters,
    local_basis,
    local_to_world,
    locator_hole_local_positions,
    m4_pin_local_positions,
    rotate_mating_pattern_local,
)


def build_contract(params: StandParameters) -> dict:
    male_pins = [
        {
            "local_mm": [x_mm, v_mm],
            "diameter_mm": params.printed_pin_diameter_mm,
            "height_mm": params.printed_pin_height_mm,
        }
        for x_mm, v_mm in m4_pin_local_positions(params)
    ]
    pin_positions = m4_pin_local_positions(params)
    locator_positions = locator_hole_local_positions(params)
    female_holes = [
        {
            "local_mm": [x_mm, v_mm],
            "diameter_mm": diameter_mm,
            "depth_mm": params.locator_hole_depth_mm,
        }
        for (x_mm, v_mm), diameter_mm in zip(
            locator_positions, params.locator_hole_diameters_mm
        )
    ]
    module_locator_posts = [
        {
            "local_mm": [x_mm, v_mm],
            "diameter_mm": diameter_mm,
            "height_mm": 3.5,
        }
        for (x_mm, v_mm), diameter_mm in zip(
            locator_positions, params.locator_post_diameters_mm
        )
    ]
    return {
        "print_object": "STAND_PRINT",
        "reference_object": "MODULE_REFERENCE",
        "mating_angle_deg": params.angle_deg,
        "base_mm": [
            params.base_width_mm,
            params.base_depth_mm,
            params.base_thickness_mm,
        ],
        "base_corner_radius_mm": params.base_corner_radius_mm,
        "wedge_width_mm": params.wedge_width_mm,
        "wedge_y_range_mm": [params.wedge_y_front_mm, params.wedge_y_rear_mm],
        "mating_pad_mm": [
            params.mating_pad_diameter_mm,
            params.mating_pad_proud_mm,
        ],
        "c_ring": {
            "inner_diameter_mm": params.c_ring_inner_diameter_mm,
            "outer_diameter_mm": params.c_ring_outer_diameter_mm,
            "height_mm": params.c_ring_height_mm,
            "opening_width_mm": params.fpc_groove_width_mm,
            "opening_direction": (
                f"clockwise_{params.mating_pattern_rotation_clockwise_deg:g}"
                "_from_front_lower"
            ),
        },
        "display_orientation": {
            "mechanical_rotation_clockwise_deg": (
                params.mating_pattern_rotation_clockwise_deg
            ),
            "six_oclock_pin_index": params.display_six_oclock_pin_index,
            "six_oclock_pin_local_mm": list(
                pin_positions[
                    params.display_six_oclock_pin_index - 1
                ]
            ),
            "nominal_firmware_rotation_deg": (
                params.display_rotation_nominal_deg
            ),
        },
        "male_pins": male_pins,
        "female_holes": female_holes,
        "module_locator_posts": module_locator_posts,
        "fpc_groove": {
            "width_mm": params.fpc_groove_width_mm,
            "depth_mm": params.fpc_groove_depth_mm,
            "v_range_mm": [
                params.fpc_groove_v_min_mm,
                params.fpc_groove_v_max_mm,
            ],
            "transition_v_range_mm": [
                params.fpc_groove_transition_v_min_mm,
                params.fpc_groove_transition_v_max_mm,
            ],
            "rotation_clockwise_deg": (
                params.mating_pattern_rotation_clockwise_deg
            ),
            "exit": "front_lower_after_clockwise_rotation",
        },
    }


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for curve in list(bpy.data.curves):
        if curve.users == 0:
            bpy.data.curves.remove(curve)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)


def create_collection(name: str) -> bpy.types.Collection:
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(
    obj: bpy.types.Object, collection: bpy.types.Collection
) -> None:
    for current in list(obj.users_collection):
        current.objects.unlink(obj)
    collection.objects.link(obj)


def select_only(obj: bpy.types.Object) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    obj.hide_set(False)
    obj.hide_viewport = False
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def apply_transform(obj: bpy.types.Object) -> None:
    select_only(obj)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)


def basis_vectors(
    params: StandParameters,
) -> tuple[Vector, Vector, Vector]:
    return tuple(Vector(axis) for axis in local_basis(params))


def world_point(
    x_mm: float,
    v_mm: float,
    normal_mm: float,
    params: StandParameters,
) -> Vector:
    return Vector(local_to_world(x_mm, v_mm, normal_mm, params))


def pattern_world_point(
    x_mm: float,
    v_mm: float,
    normal_mm: float,
    params: StandParameters,
) -> Vector:
    rotated_x, rotated_v = rotate_mating_pattern_local(
        x_mm, v_mm, params
    )
    return world_point(rotated_x, rotated_v, normal_mm, params)


def create_material(
    name: str,
    color: tuple[float, float, float, float],
    metallic: float = 0.0,
    roughness: float = 0.45,
) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = color
    principled = next(
        (
            node
            for node in material.node_tree.nodes
            if node.type == "BSDF_PRINCIPLED"
        ),
        None,
    )
    if principled is not None:
        principled.inputs["Base Color"].default_value = color
        principled.inputs["Metallic"].default_value = metallic
        principled.inputs["Roughness"].default_value = roughness
        principled.inputs["Alpha"].default_value = color[3]
    return material


def assign_material(obj: bpy.types.Object, material: bpy.types.Material) -> None:
    if obj.data and hasattr(obj.data, "materials"):
        obj.data.materials.clear()
        obj.data.materials.append(material)


def create_rounded_base(params: StandParameters) -> bpy.types.Object:
    half_width = params.base_width_mm / 2.0
    half_depth = params.base_depth_mm / 2.0
    radius = params.base_corner_radius_mm
    segments = 10
    points: list[tuple[float, float]] = []
    corners = (
        (half_width - radius, half_depth - radius, 0.0),
        (-half_width + radius, half_depth - radius, 90.0),
        (-half_width + radius, -half_depth + radius, 180.0),
        (half_width - radius, -half_depth + radius, 270.0),
    )
    for center_x, center_y, start_deg in corners:
        for index in range(segments + 1):
            angle = math.radians(start_deg + 90.0 * index / segments)
            points.append(
                (
                    center_x + radius * math.cos(angle),
                    center_y + radius * math.sin(angle),
                )
            )

    z_bottom = 0.0
    z_top = params.base_thickness_mm
    vertices = [(x, y, z_bottom) for x, y in points]
    vertices.extend((x, y, z_top) for x, y in points)
    count = len(points)
    faces = [tuple(reversed(range(count))), tuple(range(count, count * 2))]
    for index in range(count):
        next_index = (index + 1) % count
        faces.append((index, next_index, count + next_index, count + index))

    mesh = bpy.data.meshes.new("BASE_MESH")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=True)
    mesh.update()
    base = bpy.data.objects.new("BASE_SOLID", mesh)
    bpy.context.scene.collection.objects.link(base)
    return base


def wedge_top_z(y_mm: float, params: StandParameters) -> float:
    _, _, normal = basis_vectors(params)
    mating_center = Vector(params.mating_center_mm)
    wedge_plane_point = mating_center - normal * params.mating_pad_proud_mm
    return wedge_plane_point.z + math.tan(math.radians(params.angle_deg)) * (
        y_mm - wedge_plane_point.y
    )


def create_wedge(params: StandParameters) -> bpy.types.Object:
    half_width = params.wedge_width_mm / 2.0
    y_front = params.wedge_y_front_mm
    y_rear = params.wedge_y_rear_mm
    z_bottom = params.base_thickness_mm - 0.5
    z_front = wedge_top_z(y_front, params)
    z_rear = wedge_top_z(y_rear, params)
    vertices = [
        (-half_width, y_front, z_bottom),
        (half_width, y_front, z_bottom),
        (half_width, y_rear, z_bottom),
        (-half_width, y_rear, z_bottom),
        (-half_width, y_front, z_front),
        (half_width, y_front, z_front),
        (half_width, y_rear, z_rear),
        (-half_width, y_rear, z_rear),
    ]
    faces = [
        (0, 3, 2, 1),
        (0, 1, 5, 4),
        (1, 2, 6, 5),
        (2, 3, 7, 6),
        (3, 0, 4, 7),
        (4, 5, 6, 7),
    ]
    mesh = bpy.data.meshes.new("WEDGE_MESH")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=True)
    mesh.update()
    wedge = bpy.data.objects.new("WEDGE_SOLID", mesh)
    bpy.context.scene.collection.objects.link(wedge)
    return wedge


def oriented_cylinder(
    name: str,
    radius_mm: float,
    depth_mm: float,
    center: Vector,
    params: StandParameters,
    vertices: int = 96,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius_mm,
        depth=depth_mm,
        location=center,
        rotation=(math.radians(params.angle_deg), 0.0, 0.0),
    )
    obj = bpy.context.object
    obj.name = name
    apply_transform(obj)
    return obj


def create_mating_pad(params: StandParameters) -> bpy.types.Object:
    _, _, normal = basis_vectors(params)
    overlap_mm = 0.5
    depth_mm = params.mating_pad_proud_mm + overlap_mm
    center = Vector(params.mating_center_mm) - normal * (depth_mm / 2.0)
    return oriented_cylinder(
        "MATING_PAD_SOLID",
        params.mating_pad_diameter_mm / 2.0,
        depth_mm,
        center,
        params,
    )


def apply_boolean(
    target: bpy.types.Object,
    operand: bpy.types.Object,
    operation: str,
) -> None:
    target.hide_set(False)
    operand.hide_set(False)
    modifier = target.modifiers.new(
        name=f"{operation}_{operand.name}", type="BOOLEAN"
    )
    modifier.operation = operation
    modifier.solver = "EXACT"
    modifier.object = operand
    select_only(target)
    result = bpy.ops.object.modifier_apply(modifier=modifier.name)
    if "FINISHED" not in result:
        raise RuntimeError(
            f"boolean {operation} failed: {target.name} with {operand.name}"
        )
    bpy.data.objects.remove(operand, do_unlink=True)


def create_alignment_pin(
    name: str,
    point: Vector,
    normal: Vector,
    params: StandParameters,
) -> bpy.types.Object:
    u_axis, v_axis, _ = basis_vectors(params)
    tip_height = params.printed_pin_tip_height_mm
    shaft_height = params.printed_pin_height_mm - tip_height
    radius = params.printed_pin_diameter_mm / 2.0
    ring_specs = (
        (-0.15, radius),
        (shaft_height, radius),
        (params.printed_pin_height_mm, radius - 0.4),
    )
    segments = 64
    vertices: list[tuple[float, float, float]] = []
    for normal_offset, ring_radius in ring_specs:
        ring_center = point + normal * normal_offset
        for index in range(segments):
            angle = 2.0 * math.pi * index / segments
            vertex = (
                ring_center
                + u_axis * (ring_radius * math.cos(angle))
                + v_axis * (ring_radius * math.sin(angle))
            )
            vertices.append(tuple(vertex))

    faces: list[tuple[int, ...]] = []
    faces.append(tuple(reversed(range(segments))))
    for ring_index in range(len(ring_specs) - 1):
        lower_start = ring_index * segments
        upper_start = (ring_index + 1) * segments
        for index in range(segments):
            next_index = (index + 1) % segments
            faces.append(
                (
                    lower_start + index,
                    lower_start + next_index,
                    upper_start + next_index,
                    upper_start + index,
                )
            )
    top_start = (len(ring_specs) - 1) * segments
    faces.append(tuple(top_start + index for index in range(segments)))

    mesh = bpy.data.meshes.new(f"{name}_MESH")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=True)
    mesh.update()
    pin = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(pin)
    return pin


def create_cylindrical_cutter(
    name: str,
    point: Vector,
    normal: Vector,
    diameter_mm: float,
    depth_mm: float,
    params: StandParameters,
) -> bpy.types.Object:
    front_overlap = 0.2
    cutter_depth = depth_mm + front_overlap
    center = point - normal * ((depth_mm - front_overlap) / 2.0)
    return oriented_cylinder(
        name,
        diameter_mm / 2.0,
        cutter_depth,
        center,
        params,
        vertices=64,
    )


def create_oriented_box(
    name: str,
    dimensions: tuple[float, float, float],
    center: Vector,
    params: StandParameters,
    in_plane_clockwise_deg: float = 0.0,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(location=center)
    box = bpy.context.object
    box.name = name
    box.dimensions = dimensions
    box.rotation_mode = "QUATERNION"
    stand_tilt = Matrix.Rotation(math.radians(params.angle_deg), 4, "X")
    in_plane = Matrix.Rotation(
        math.radians(-in_plane_clockwise_deg), 4, "Z"
    )
    box.rotation_quaternion = (stand_tilt @ in_plane).to_quaternion()
    apply_transform(box)
    return box


def create_c_ring(params: StandParameters) -> bpy.types.Object:
    overlap_mm = 0.4
    edge_inset_mm = 0.1
    inner_radius = params.c_ring_inner_diameter_mm / 2.0
    outer_radius = params.c_ring_outer_diameter_mm / 2.0
    segments = 128
    profiles = (
        (-overlap_mm, outer_radius - edge_inset_mm),
        (0.2, outer_radius),
        (params.c_ring_height_mm - 0.1, outer_radius),
        (params.c_ring_height_mm, outer_radius - edge_inset_mm),
    )
    u_axis, v_axis, normal = basis_vectors(params)
    mating_center = Vector(params.mating_center_mm)
    vertices: list[tuple[float, float, float]] = []

    for normal_mm, profile_outer_radius in profiles:
        for radius in (profile_outer_radius, inner_radius):
            for index in range(segments):
                angle = 2.0 * math.pi * index / segments
                point = (
                    mating_center
                    + u_axis * (radius * math.cos(angle))
                    + v_axis * (radius * math.sin(angle))
                    + normal * normal_mm
                )
                vertices.append(tuple(point))

    def vertex_index(level: int, inner: bool, segment: int) -> int:
        return level * segments * 2 + int(inner) * segments + segment % segments

    faces: list[tuple[int, ...]] = []
    for level in range(len(profiles) - 1):
        for index in range(segments):
            next_index = (index + 1) % segments
            faces.append(
                (
                    vertex_index(level, False, index),
                    vertex_index(level, False, next_index),
                    vertex_index(level + 1, False, next_index),
                    vertex_index(level + 1, False, index),
                )
            )
            faces.append(
                (
                    vertex_index(level, True, index),
                    vertex_index(level + 1, True, index),
                    vertex_index(level + 1, True, next_index),
                    vertex_index(level, True, next_index),
                )
            )

    top_level = len(profiles) - 1
    for index in range(segments):
        next_index = (index + 1) % segments
        faces.append(
            (
                vertex_index(0, False, index),
                vertex_index(0, True, index),
                vertex_index(0, True, next_index),
                vertex_index(0, False, next_index),
            )
        )
        faces.append(
            (
                vertex_index(top_level, False, index),
                vertex_index(top_level, False, next_index),
                vertex_index(top_level, True, next_index),
                vertex_index(top_level, True, index),
            )
        )

    mesh = bpy.data.meshes.new("C_RING_MESH")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=True)
    mesh.update()
    ring = bpy.data.objects.new("C_RING_SOLID", mesh)
    bpy.context.scene.collection.objects.link(ring)

    opening_center_v = (
        params.fpc_groove_v_min_mm + params.fpc_groove_v_max_mm
    ) / 2.0
    center_normal = (params.c_ring_height_mm - overlap_mm) / 2.0
    opening = create_oriented_box(
        "C_RING_OPENING_CUTTER",
        (
            params.fpc_groove_width_mm,
            params.fpc_groove_v_max_mm - params.fpc_groove_v_min_mm,
            params.c_ring_height_mm + overlap_mm + 0.2,
        ),
        pattern_world_point(0.0, opening_center_v, center_normal, params),
        params,
        params.mating_pattern_rotation_clockwise_deg,
    )
    apply_boolean(ring, opening, "DIFFERENCE")
    return ring


def create_fpc_wedge_groove_cutter(
    params: StandParameters,
    top_normal_mm: float,
    deep_bottom_normal_mm: float,
) -> bpy.types.Object:
    half_width = params.fpc_groove_width_mm / 2.0
    sections = (
        (params.fpc_groove_v_min_mm, deep_bottom_normal_mm),
        (params.fpc_groove_transition_v_min_mm, deep_bottom_normal_mm),
        (
            params.fpc_groove_transition_v_max_mm + 0.2,
            -params.fpc_groove_depth_mm + 0.05,
        ),
    )
    local_vertices = []
    for v_mm, bottom_normal_mm in sections:
        local_vertices.extend(
            (
                (-half_width, v_mm, top_normal_mm),
                (half_width, v_mm, top_normal_mm),
                (half_width, v_mm, bottom_normal_mm),
                (-half_width, v_mm, bottom_normal_mm),
            )
        )
    vertices = [
        tuple(pattern_world_point(x, v, n, params))
        for x, v, n in local_vertices
    ]
    faces = [(0, 3, 2, 1)]
    for section_index in range(len(sections) - 1):
        current = section_index * 4
        following = (section_index + 1) * 4
        faces.extend(
            (
                (current, current + 1, following + 1, following),
                (current + 1, current + 2, following + 2, following + 1),
                (current + 2, current + 3, following + 3, following + 2),
                (current + 3, current, following, following + 3),
            )
        )
    last = (len(sections) - 1) * 4
    faces.append((last, last + 1, last + 2, last + 3))
    mesh = bpy.data.meshes.new("FPC_WEDGE_GROOVE_CUTTER_MESH")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=True)
    mesh.update()
    cutter = bpy.data.objects.new("FPC_WEDGE_GROOVE_CUTTER", mesh)
    bpy.context.scene.collection.objects.link(cutter)
    return cutter


def create_reference_empty(
    name: str,
    location: Vector,
    collection: bpy.types.Collection,
    display_type: str = "ARROWS",
) -> bpy.types.Object:
    empty = bpy.data.objects.new(name, None)
    empty.location = location
    empty.empty_display_type = display_type
    empty.empty_display_size = 4.0
    collection.objects.link(empty)
    return empty


def create_fpc_reference(
    params: StandParameters,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> bpy.types.Object:
    curve_data = bpy.data.curves.new("FPC_PATH_CURVE", type="CURVE")
    curve_data.dimensions = "3D"
    curve_data.bevel_depth = 0.7
    curve_data.bevel_resolution = 3
    spline = curve_data.splines.new(type="POLY")
    spline.points.add(2)
    for point, v_mm in zip(
        spline.points,
        (
            params.fpc_groove_v_max_mm,
            -10.0,
            params.fpc_groove_v_min_mm,
        ),
    ):
        position = pattern_world_point(0.0, v_mm, 0.35, params)
        point.co = (*position, 1.0)
    fpc = bpy.data.objects.new("FPC_PATH_REFERENCE", curve_data)
    collection.objects.link(fpc)
    assign_material(fpc, material)
    return fpc


def create_reference_assembly(
    params: StandParameters,
    collection: bpy.types.Collection,
) -> dict[str, bpy.types.Object]:
    _, _, normal = basis_vectors(params)
    mating_center = Vector(params.mating_center_mm)
    module_material = create_material(
        "MODULE_REFERENCE_MATERIAL", (0.025, 0.035, 0.045, 0.38), 0.15, 0.28
    )
    screen_material = create_material(
        "SCREEN_REFERENCE_MATERIAL", (0.01, 0.16, 0.20, 0.75), 0.05, 0.2
    )
    fpc_material = create_material(
        "FPC_REFERENCE_MATERIAL", (0.95, 0.28, 0.03, 1.0), 0.0, 0.55
    )

    boss_depth = params.module_rear_interface_depth_mm
    boss_center = mating_center + normal * (boss_depth / 2.0)
    boss = oriented_cylinder(
        "MODULE_REAR_INTERFACE_REFERENCE",
        params.module_rear_interface_diameter_mm / 2.0,
        boss_depth,
        boss_center,
        params,
    )
    move_to_collection(boss, collection)
    assign_material(boss, module_material)

    body_depth = 17.9
    body_center = mating_center + normal * (boss_depth + body_depth / 2.0)
    module = oriented_cylinder(
        "MODULE_REFERENCE",
        params.module_outer_diameter_mm / 2.0,
        body_depth,
        body_center,
        params,
        vertices=128,
    )
    move_to_collection(module, collection)
    assign_material(module, module_material)

    screen_center = mating_center + normal * (boss_depth + body_depth + 0.3)
    screen = oriented_cylinder(
        "SCREEN_REFERENCE",
        27.2,
        0.6,
        screen_center,
        params,
        vertices=128,
    )
    move_to_collection(screen, collection)
    assign_material(screen, screen_material)

    for index, ((x_mm, v_mm), diameter_mm) in enumerate(
        zip(
            locator_hole_local_positions(params),
            params.locator_post_diameters_mm,
        ),
        start=1,
    ):
        post_surface = world_point(x_mm, v_mm, 0.0, params)
        post = oriented_cylinder(
            f"MODULE_LOCATOR_POST_{index}_REFERENCE",
            diameter_mm / 2.0,
            3.5,
            post_surface - normal * 1.75,
            params,
            vertices=48,
        )
        move_to_collection(post, collection)
        assign_material(post, module_material)

    m4_marker = create_reference_empty(
        "M4_PATTERN_REFERENCE", mating_center, collection, "CIRCLE"
    )
    m4_marker["pcd_mm"] = params.m4_pcd_mm
    m4_marker["positions_local_json"] = json.dumps(
        m4_pin_local_positions(params)
    )
    locator_marker = create_reference_empty(
        "LOCATOR_REFERENCE", mating_center, collection, "CIRCLE"
    )
    locator_marker["pcd_mm"] = params.locator_pcd_mm
    locator_marker["positions_local_json"] = json.dumps(
        locator_hole_local_positions(params)
    )
    m4_axes = []
    for index, (x_mm, v_mm) in enumerate(m4_pin_local_positions(params), start=1):
        marker = create_reference_empty(
            f"M4_AXIS_{index}_REFERENCE",
            world_point(x_mm, v_mm, 0.0, params),
            collection,
            "SINGLE_ARROW",
        )
        marker.rotation_euler = (math.radians(params.angle_deg), 0.0, 0.0)
        marker.empty_display_size = 2.5
        m4_axes.append(marker)

    locator_axes = []
    for index, (x_mm, v_mm) in enumerate(
        locator_hole_local_positions(params), start=1
    ):
        marker = create_reference_empty(
            f"LOCATOR_AXIS_{index}_REFERENCE",
            world_point(x_mm, v_mm, 0.0, params),
            collection,
            "SINGLE_ARROW",
        )
        marker.rotation_euler = (math.radians(params.angle_deg), 0.0, 0.0)
        marker.empty_display_size = 2.0
        locator_axes.append(marker)
    fpc = create_fpc_reference(params, collection, fpc_material)

    return {
        "module": module,
        "boss": boss,
        "screen": screen,
        "m4_marker": m4_marker,
        "locator_marker": locator_marker,
        "m4_axes": m4_axes,
        "locator_axes": locator_axes,
        "fpc": fpc,
    }


def configure_scene(params: StandParameters) -> None:
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.001
    scene.unit_settings.length_unit = "MILLIMETERS"
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene["design_name"] = "ESP32-S3 Round Display Test Stand"
    scene["design_version"] = 1
    scene["units"] = "millimeters"


def build_scene(params: StandParameters) -> dict[str, object]:
    clear_scene()
    configure_scene(params)
    print_collection = create_collection("PRINT")
    reference_collection = create_collection("REFERENCE")
    stand_material = create_material(
        "STAND_MATERIAL", (0.06, 0.075, 0.09, 1.0), 0.08, 0.34
    )

    stand = create_rounded_base(params)
    wedge = create_wedge(params)
    apply_boolean(stand, wedge, "UNION")
    pad = create_mating_pad(params)
    apply_boolean(stand, pad, "UNION")
    c_ring = create_c_ring(params)
    apply_boolean(stand, c_ring, "UNION")

    _, _, normal = basis_vectors(params)
    for index, (x_mm, v_mm) in enumerate(m4_pin_local_positions(params), start=1):
        pin_surface = world_point(x_mm, v_mm, 0.0, params)
        pin = create_alignment_pin(
            f"ALIGNMENT_PIN_{index}", pin_surface, normal, params
        )
        apply_boolean(stand, pin, "UNION")

    for index, ((x_mm, v_mm), diameter_mm) in enumerate(
        zip(
            locator_hole_local_positions(params),
            params.locator_hole_diameters_mm,
        ),
        start=1,
    ):
        hole_surface = world_point(x_mm, v_mm, 0.0, params)
        cutter = create_cylindrical_cutter(
            f"LOCATOR_HOLE_{index}_CUTTER",
            hole_surface,
            normal,
            diameter_mm,
            params.locator_hole_depth_mm,
            params,
        )
        apply_boolean(stand, cutter, "DIFFERENCE")

    pad_groove_center_v = (
        params.fpc_groove_v_min_mm + params.fpc_groove_v_max_mm
    ) / 2.0
    pad_groove_length = params.fpc_groove_v_max_mm - params.fpc_groove_v_min_mm
    pad_groove_center = pattern_world_point(
        0.0,
        pad_groove_center_v,
        -params.fpc_groove_depth_mm / 2.0 + 0.05,
        params,
    )
    pad_groove = create_oriented_box(
        "FPC_PAD_GROOVE_CUTTER",
        (
            params.fpc_groove_width_mm,
            pad_groove_length,
            params.fpc_groove_depth_mm + 0.2,
        ),
        pad_groove_center,
        params,
        params.mating_pattern_rotation_clockwise_deg,
    )
    apply_boolean(stand, pad_groove, "DIFFERENCE")

    wedge_surface_normal = -params.mating_pad_proud_mm
    wedge_front_normal = wedge_surface_normal + 0.2
    wedge_back_normal = (
        wedge_surface_normal - params.fpc_groove_depth_mm - 0.05
    )
    wedge_groove = create_fpc_wedge_groove_cutter(
        params,
        wedge_front_normal,
        wedge_back_normal,
    )
    apply_boolean(stand, wedge_groove, "DIFFERENCE")

    stand.name = "STAND_PRINT"
    move_to_collection(stand, print_collection)
    assign_material(stand, stand_material)
    stand["mating_angle_deg"] = params.angle_deg
    stand["mating_center_mm_json"] = json.dumps(params.mating_center_mm)
    stand["male_pin_count"] = 3
    stand["male_pin_diameter_mm"] = params.printed_pin_diameter_mm
    stand["male_pin_height_mm"] = params.printed_pin_height_mm
    stand["male_pin_positions_local_json"] = json.dumps(
        m4_pin_local_positions(params)
    )
    stand["female_hole_count"] = 2
    stand["female_hole_diameters_mm_json"] = json.dumps(
        params.locator_hole_diameters_mm
    )
    stand["female_hole_depth_mm"] = params.locator_hole_depth_mm
    stand["female_hole_positions_local_json"] = json.dumps(
        locator_hole_local_positions(params)
    )
    stand["fpc_groove_width_mm"] = params.fpc_groove_width_mm
    stand["fpc_groove_depth_mm"] = params.fpc_groove_depth_mm
    stand["c_ring_inner_diameter_mm"] = params.c_ring_inner_diameter_mm
    stand["c_ring_outer_diameter_mm"] = params.c_ring_outer_diameter_mm
    stand["c_ring_height_mm"] = params.c_ring_height_mm
    stand["c_ring_opening_width_mm"] = params.fpc_groove_width_mm
    stand["display_six_oclock_pin_index"] = (
        params.display_six_oclock_pin_index
    )
    stand["mating_pattern_rotation_clockwise_deg"] = (
        params.mating_pattern_rotation_clockwise_deg
    )
    stand["display_rotation_nominal_deg"] = params.display_rotation_nominal_deg
    stand["uses_screws"] = False

    references = create_reference_assembly(params, reference_collection)
    select_only(stand)
    bpy.context.view_layer.update()
    return {
        "stand": stand,
        "references": references,
        "print_collection": print_collection,
        "reference_collection": reference_collection,
    }


def save_and_export(stand: bpy.types.Object) -> tuple[Path, Path]:
    models_dir = ROOT_DIR / "outputs" / "models"
    models_dir.mkdir(parents=True, exist_ok=True)
    blend_path = models_dir / "round_display_test_stand.blend"
    stl_path = models_dir / "round_display_test_stand.stl"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
    export_mesh = None
    export_object = None
    try:
        export_mesh = stand.data.copy()
        export_object = stand.copy()
        export_object.name = "STAND_STL_EXPORT"
        export_object.data = export_mesh
        bpy.context.scene.collection.objects.link(export_object)
        triangulated = bmesh.new()
        try:
            triangulated.from_mesh(export_mesh)
            bmesh.ops.remove_doubles(
                triangulated,
                verts=list(triangulated.verts),
                dist=0.0001,
            )
            bmesh.ops.dissolve_degenerate(
                triangulated,
                edges=list(triangulated.edges),
                dist=0.0001,
            )
            bmesh.ops.triangulate(
                triangulated,
                faces=list(triangulated.faces),
                quad_method="BEAUTY",
                ngon_method="BEAUTY",
            )
            bmesh.ops.recalc_face_normals(
                triangulated,
                faces=list(triangulated.faces),
            )
            triangulated.to_mesh(export_mesh)
            export_mesh.update()
        finally:
            triangulated.free()

        select_only(export_object)
        bpy.ops.wm.stl_export(
            filepath=str(stl_path),
            check_existing=False,
            export_selected_objects=True,
            apply_modifiers=True,
            ascii_format=False,
            use_scene_unit=False,
            global_scale=1.0,
        )
    finally:
        if export_object is not None and export_object.name in bpy.data.objects:
            bpy.data.objects.remove(export_object, do_unlink=True)
        if (
            export_mesh is not None
            and export_mesh.name in bpy.data.meshes
            and export_mesh.users == 0
        ):
            bpy.data.meshes.remove(export_mesh)
        select_only(stand)
    return blend_path, stl_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the round display stand")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else [])


def main() -> None:
    args = parse_args()
    contract = build_contract(StandParameters())
    if args.dry_run:
        print(json.dumps(contract, indent=2, sort_keys=True))
        return
    artifacts = build_scene(StandParameters())
    blend_path, stl_path = save_and_export(artifacts["stand"])
    print(
        json.dumps(
            {
                "status": "BUILD_OK",
                "blend": str(blend_path),
                "stl": str(stl_path),
                "contract": contract,
            },
            indent=2,
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
