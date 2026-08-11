import json
import math
from pathlib import Path
import struct
import sys

import bmesh
import bpy
from mathutils import Vector


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from stand_parameters import (  # noqa: E402
    StandParameters,
    conservative_module_clearance_mm,
    local_basis,
    local_to_world,
    locator_hole_local_positions,
    m4_pin_local_positions,
    rotate_mating_pattern_local,
)


def object_world_bounds(obj: bpy.types.Object) -> dict[str, list[float]]:
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    minimum = [min(corner[axis] for corner in corners) for axis in range(3)]
    maximum = [max(corner[axis] for corner in corners) for axis in range(3)]
    dimensions = [maximum[axis] - minimum[axis] for axis in range(3)]
    return {
        "minimum_mm": [round(value, 6) for value in minimum],
        "maximum_mm": [round(value, 6) for value in maximum],
        "dimensions_mm": [round(value, 6) for value in dimensions],
    }


def mating_basis(params: StandParameters) -> tuple[Vector, Vector, Vector]:
    return tuple(Vector(axis) for axis in local_basis(params))


def world_to_mating_local(
    point: Vector, params: StandParameters
) -> tuple[float, float, float]:
    u_axis, v_axis, normal = mating_basis(params)
    delta = point - Vector(params.mating_center_mm)
    return (delta.dot(u_axis), delta.dot(v_axis), delta.dot(normal))


def mating_world_point(
    x_mm: float, v_mm: float, normal_mm: float, params: StandParameters
) -> Vector:
    return Vector(local_to_world(x_mm, v_mm, normal_mm, params))


def raycast_mating_surface(
    obj: bpy.types.Object,
    origin_mating: tuple[float, float, float],
    direction_world: Vector,
    params: StandParameters,
) -> tuple[float, float, float]:
    origin_world = mating_world_point(*origin_mating, params)
    inverse = obj.matrix_world.inverted()
    origin_object = inverse @ origin_world
    direction_object = (inverse.to_3x3() @ direction_world).normalized()
    hit, location_object, _, _ = obj.ray_cast(origin_object, direction_object)
    if not hit:
        raise ValueError(f"ray missed stand from mating coordinate {origin_mating}")
    location_world = obj.matrix_world @ location_object
    return world_to_mating_local(location_world, params)


def measure_mating_plane(
    stand: bpy.types.Object, params: StandParameters
) -> dict[str, object]:
    _, _, expected_normal = mating_basis(params)
    sample_coordinates = ((-12.0, 10.0), (12.0, 10.0), (0.0, 15.0))
    points = []
    offsets = []
    for x_mm, v_mm in sample_coordinates:
        local_hit = raycast_mating_surface(
            stand, (x_mm, v_mm, 5.0), -expected_normal, params
        )
        points.append(mating_world_point(*local_hit, params))
        offsets.append(local_hit[2])

    measured_normal = (points[1] - points[0]).cross(points[2] - points[0])
    measured_normal.normalize()
    if measured_normal.z < 0.0:
        measured_normal.negate()
    angle_deg = math.degrees(
        math.acos(max(-1.0, min(1.0, measured_normal.z)))
    )
    return {
        "angle_deg": round(angle_deg, 6),
        "normal": [round(value, 6) for value in measured_normal],
        "sample_normal_offsets_mm": [round(value, 6) for value in offsets],
    }


def mesh_vertices_in_mating_coordinates(
    stand: bpy.types.Object, params: StandParameters
) -> list[tuple[float, float, float]]:
    return [
        world_to_mating_local(stand.matrix_world @ vertex.co, params)
        for vertex in stand.data.vertices
    ]


def measure_male_pins(
    stand: bpy.types.Object, params: StandParameters
) -> list[dict[str, object]]:
    vertices = mesh_vertices_in_mating_coordinates(stand, params)
    shaft_ring_normal = (
        params.printed_pin_height_mm - params.printed_pin_tip_height_mm
    )
    results = []
    for expected_x, expected_v in m4_pin_local_positions(params):
        ring = [
            point
            for point in vertices
            if abs(point[2] - shaft_ring_normal) < 0.04
            and math.hypot(point[0] - expected_x, point[1] - expected_v) < 2.2
        ]
        if len(ring) < 16:
            raise ValueError(
                f"could not measure alignment pin near {(expected_x, expected_v)}"
            )
        center_x = sum(point[0] for point in ring) / len(ring)
        center_v = sum(point[1] for point in ring) / len(ring)
        radii = [
            math.hypot(point[0] - center_x, point[1] - center_v)
            for point in ring
        ]
        nearby = [
            point
            for point in vertices
            if point[2] > 0.1
            and math.hypot(point[0] - center_x, point[1] - center_v) < 2.2
        ]
        results.append(
            {
                "center_local_mm": [round(center_x, 6), round(center_v, 6)],
                "diameter_mm": round(2.0 * sum(radii) / len(radii), 6),
                "height_mm": round(max(point[2] for point in nearby), 6),
            }
        )
    return results


def measure_female_holes(
    stand: bpy.types.Object, params: StandParameters
) -> list[dict[str, object]]:
    u_axis, v_axis, normal = mating_basis(params)
    measurement_normal = -params.locator_hole_depth_mm / 2.0
    results = []
    for expected_x, expected_v in locator_hole_local_positions(params):
        bottom = raycast_mating_surface(
            stand, (expected_x, expected_v, 5.0), -normal, params
        )
        left = raycast_mating_surface(
            stand,
            (expected_x - 5.0, expected_v, measurement_normal),
            u_axis,
            params,
        )
        right = raycast_mating_surface(
            stand,
            (expected_x + 5.0, expected_v, measurement_normal),
            -u_axis,
            params,
        )
        lower = raycast_mating_surface(
            stand,
            (expected_x, expected_v - 5.0, measurement_normal),
            v_axis,
            params,
        )
        upper = raycast_mating_surface(
            stand,
            (expected_x, expected_v + 5.0, measurement_normal),
            -v_axis,
            params,
        )
        results.append(
            {
                "center_local_mm": [
                    round((left[0] + right[0]) / 2.0, 6),
                    round((lower[1] + upper[1]) / 2.0, 6),
                ],
                "diameter_u_mm": round(right[0] - left[0], 6),
                "diameter_v_mm": round(upper[1] - lower[1], 6),
                "depth_mm": round(-bottom[2], 6),
            }
        )
    return results


def measure_fpc_groove(
    stand: bpy.types.Object, params: StandParameters
) -> dict[str, float]:
    u_axis, v_axis, normal = mating_basis(params)
    angle = math.radians(params.mating_pattern_rotation_clockwise_deg)
    cross_local = Vector((math.cos(angle), -math.sin(angle)))
    cross_world = (u_axis * cross_local.x + v_axis * cross_local.y).normalized()
    pad_v = -10.0
    transition_v = (
        params.fpc_groove_transition_v_min_mm
        + params.fpc_groove_transition_v_max_mm
    ) / 2.0
    wedge_v = -24.0
    def centerline(v_mm: float, normal_mm: float) -> tuple[float, float, float]:
        x_rotated, v_rotated = rotate_mating_pattern_local(0.0, v_mm, params)
        return (x_rotated, v_rotated, normal_mm)

    pad_floor = raycast_mating_surface(
        stand, centerline(pad_v, 6.0), -normal, params
    )
    transition_floor = raycast_mating_surface(
        stand, centerline(transition_v, 6.0), -normal, params
    )
    wedge_floor = raycast_mating_surface(
        stand, centerline(wedge_v, 6.0), -normal, params
    )

    def groove_width(v_mm: float, normal_mm: float) -> float:
        center_x, center_v = rotate_mating_pattern_local(0.0, v_mm, params)
        left_origin = (
            center_x - cross_local.x * 10.0,
            center_v - cross_local.y * 10.0,
            normal_mm,
        )
        right_origin = (
            center_x + cross_local.x * 10.0,
            center_v + cross_local.y * 10.0,
            normal_mm,
        )
        left = raycast_mating_surface(
            stand, left_origin, cross_world, params
        )
        right = raycast_mating_surface(
            stand, right_origin, -cross_world, params
        )
        delta = Vector((right[0] - left[0], right[1] - left[1]))
        return delta.dot(cross_local)

    wedge_surface = -params.mating_pad_proud_mm
    return {
        "pad_depth_mm": round(-pad_floor[2], 6),
        "transition_floor_normal_mm": round(transition_floor[2], 6),
        "wedge_depth_mm": round(wedge_surface - wedge_floor[2], 6),
        "pad_width_mm": round(groove_width(pad_v, -1.0), 6),
        "wedge_width_mm": round(
            groove_width(
                wedge_v, wedge_surface - params.fpc_groove_depth_mm / 2.0
            ),
            6,
        ),
    }


def measure_c_ring(
    stand: bpy.types.Object, params: StandParameters
) -> dict[str, object]:
    u_axis, v_axis, normal = mating_basis(params)
    angle = math.radians(params.mating_pattern_rotation_clockwise_deg)
    cross_local = Vector((math.cos(angle), -math.sin(angle)))
    cross_world = (u_axis * cross_local.x + v_axis * cross_local.y).normalized()
    mid_height = params.c_ring_height_mm / 2.0
    wall_center = (
        params.c_ring_inner_diameter_mm + params.c_ring_outer_diameter_mm
    ) / 4.0
    opening_sample_heights = (
        0.05,
        mid_height,
        params.c_ring_height_mm - 0.05,
    )

    inner_left = raycast_mating_surface(
        stand, (0.0, 0.0, mid_height), -u_axis, params
    )
    inner_right = raycast_mating_surface(
        stand, (0.0, 0.0, mid_height), u_axis, params
    )
    outer_left = raycast_mating_surface(
        stand, (-25.0, 0.0, mid_height), u_axis, params
    )
    outer_right = raycast_mating_surface(
        stand, (25.0, 0.0, mid_height), -u_axis, params
    )
    opening_samples = []
    opening_center = rotate_mating_pattern_local(0.0, -19.0, params)
    for sample_height in opening_sample_heights:
        opening_origin = (
            opening_center[0],
            opening_center[1],
            sample_height,
        )
        opening_left = raycast_mating_surface(
            stand, opening_origin, -cross_world, params
        )
        opening_right = raycast_mating_surface(
            stand, opening_origin, cross_world, params
        )
        opening_delta = Vector(
            (
                opening_right[0] - opening_left[0],
                opening_right[1] - opening_left[1],
            )
        )
        opening_samples.append(
            {
                "normal_mm": round(sample_height, 6),
                "width_mm": round(opening_delta.dot(cross_local), 6),
            }
        )
    top = raycast_mating_surface(
        stand, (wall_center, 0.0, 12.0), -normal, params
    )
    return {
        "inner_diameter_mm": round(inner_right[0] - inner_left[0], 6),
        "outer_diameter_mm": round(outer_right[0] - outer_left[0], 6),
        "height_mm": round(top[2], 6),
        "opening_width_mm": opening_samples[1]["width_mm"],
        "opening_width_samples_mm": opening_samples,
    }


def measure_mating_geometry(
    stand: bpy.types.Object, params: StandParameters
) -> dict[str, object]:
    plane = measure_mating_plane(stand, params)
    return {
        "mating_angle_deg": plane["angle_deg"],
        "mating_plane_normal": plane["normal"],
        "mating_plane_sample_offsets_mm": plane["sample_normal_offsets_mm"],
        "male_pins": measure_male_pins(stand, params),
        "female_holes": measure_female_holes(stand, params),
        "fpc_groove": measure_fpc_groove(stand, params),
        "c_ring": measure_c_ring(stand, params),
    }


def mesh_component_count(obj: bpy.types.Object) -> int:
    mesh = obj.data
    bm = bmesh.new()
    try:
        bm.from_mesh(mesh)
        bm.faces.ensure_lookup_table()
        remaining = set(bm.faces)
        components = 0
        while remaining:
            components += 1
            stack = [remaining.pop()]
            while stack:
                face = stack.pop()
                for edge in face.edges:
                    for neighbor in edge.link_faces:
                        if neighbor in remaining:
                            remaining.remove(neighbor)
                            stack.append(neighbor)
        return components
    finally:
        bm.free()


def non_manifold_edge_count(obj: bpy.types.Object) -> int:
    bm = bmesh.new()
    try:
        bm.from_mesh(obj.data)
        return sum(1 for edge in bm.edges if not edge.is_manifold)
    finally:
        bm.free()


def read_binary_stl_triangles(
    path: Path,
) -> list[tuple[tuple[float, float, float], ...]]:
    with path.open("rb") as handle:
        header = handle.read(80)
        count_bytes = handle.read(4)
        if len(header) != 80 or len(count_bytes) != 4:
            raise ValueError(f"invalid binary STL header: {path}")
        triangle_count = struct.unpack("<I", count_bytes)[0]
        triangles = []
        for triangle_index in range(triangle_count):
            record = handle.read(50)
            if len(record) != 50:
                raise ValueError(
                    f"truncated STL at triangle {triangle_index}: {path}"
                )
            values = struct.unpack("<12fH", record)
            coordinates = values[3:12]
            triangles.append(
                tuple(
                    tuple(
                        coordinates[vertex_index * 3 + axis]
                        for axis in range(3)
                    )
                    for vertex_index in range(3)
                )
            )
        if handle.read(1):
            raise ValueError(f"unexpected trailing STL data: {path}")
    if not triangles:
        raise ValueError(f"STL contains no triangles: {path}")
    return triangles


def triangle_double_area(
    triangle: tuple[tuple[float, float, float], ...],
) -> float:
    first, second, third = triangle
    first_edge = tuple(second[axis] - first[axis] for axis in range(3))
    second_edge = tuple(third[axis] - first[axis] for axis in range(3))
    cross = (
        first_edge[1] * second_edge[2] - first_edge[2] * second_edge[1],
        first_edge[2] * second_edge[0] - first_edge[0] * second_edge[2],
        first_edge[0] * second_edge[1] - first_edge[1] * second_edge[0],
    )
    return math.sqrt(sum(value * value for value in cross))


def read_binary_stl_bounds(path: Path) -> dict[str, object]:
    triangles = read_binary_stl_triangles(path)
    minimum = [math.inf, math.inf, math.inf]
    maximum = [-math.inf, -math.inf, -math.inf]
    edge_counts: dict[
        tuple[tuple[float, float, float], tuple[float, float, float]], int
    ] = {}
    edge_faces: dict[
        tuple[tuple[float, float, float], tuple[float, float, float]],
        list[int],
    ] = {}
    degenerate_triangle_count = 0
    valid_triangle_count = 0
    for raw_triangle in triangles:
        triangle = []
        for raw_vertex in raw_triangle:
            vertex = tuple(round(coordinate, 5) for coordinate in raw_vertex)
            triangle.append(vertex)
            for axis, value in enumerate(raw_vertex):
                minimum[axis] = min(minimum[axis], value)
                maximum[axis] = max(maximum[axis], value)
        if len(set(triangle)) != 3 or triangle_double_area(raw_triangle) <= 1e-10:
            degenerate_triangle_count += 1
            continue
        face_index = valid_triangle_count
        valid_triangle_count += 1
        for edge_index in range(3):
            start = triangle[edge_index]
            end = triangle[(edge_index + 1) % 3]
            edge = tuple(sorted((start, end)))
            edge_counts[edge] = edge_counts.get(edge, 0) + 1
            edge_faces.setdefault(edge, []).append(face_index)
    face_adjacency = {index: set() for index in range(valid_triangle_count)}
    for face_indices in edge_faces.values():
        for position, face_index in enumerate(face_indices):
            face_adjacency[face_index].update(face_indices[:position])
            face_adjacency[face_index].update(face_indices[position + 1 :])
    remaining = set(face_adjacency)
    component_count = 0
    while remaining:
        component_count += 1
        stack = [remaining.pop()]
        while stack:
            face_index = stack.pop()
            for neighbor in face_adjacency[face_index]:
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    stack.append(neighbor)
    non_manifold_edge_count = sum(
        1 for use_count in edge_counts.values() if use_count != 2
    )
    dimensions = [maximum[axis] - minimum[axis] for axis in range(3)]
    return {
        "path": str(path),
        "triangle_count": len(triangles),
        "degenerate_triangle_count": degenerate_triangle_count,
        "mesh_component_count": component_count,
        "non_manifold_edge_count": non_manifold_edge_count,
        "minimum_mm": [round(value, 6) for value in minimum],
        "maximum_mm": [round(value, 6) for value in maximum],
        "dimensions_mm": [round(value, 6) for value in dimensions],
    }


def measure_binary_stl_geometry(
    path: Path, params: StandParameters
) -> dict[str, object]:
    triangles = read_binary_stl_triangles(path)
    vertices = [vertex for triangle in triangles for vertex in triangle]
    faces = [
        (index * 3, index * 3 + 1, index * 3 + 2)
        for index in range(len(triangles))
    ]
    mesh = bpy.data.meshes.new("STL_MEASUREMENT_MESH")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    stl_object = bpy.data.objects.new("STL_MEASUREMENT", mesh)
    bpy.context.scene.collection.objects.link(stl_object)
    try:
        return measure_mating_geometry(stl_object, params)
    finally:
        bpy.data.objects.remove(stl_object, do_unlink=True)
        bpy.data.meshes.remove(mesh)


def collect_report(
    params: StandParameters,
    stand: bpy.types.Object,
    stl_path: Path,
) -> dict[str, object]:
    male_positions = json.loads(stand["male_pin_positions_local_json"])
    female_positions = json.loads(stand["female_hole_positions_local_json"])
    female_diameters = json.loads(stand["female_hole_diameters_mm_json"])
    module_clearance = conservative_module_clearance_mm(params)
    measured_geometry = measure_binary_stl_geometry(stl_path, params)
    return {
        "design": "ESP32-S3 Round Display Test Stand",
        "blender_version": bpy.app.version_string,
        "units": {
            "system": bpy.context.scene.unit_settings.system,
            "length_unit": bpy.context.scene.unit_settings.length_unit,
            "scale_length": bpy.context.scene.unit_settings.scale_length,
        },
        "stand_bounds": object_world_bounds(stand),
        "stl_bounds": read_binary_stl_bounds(stl_path),
        "mesh_component_count": mesh_component_count(stand),
        "non_manifold_edge_count": non_manifold_edge_count(stand),
        "mating_angle_deg": stand["mating_angle_deg"],
        "mating_center_mm": json.loads(stand["mating_center_mm_json"]),
        "module_clearance_mm": round(
            module_clearance, 6
        ),
        "module_clearance_above_base_mm": round(
            module_clearance - params.base_thickness_mm, 6
        ),
        "male_pins": {
            "count": stand["male_pin_count"],
            "diameter_mm": stand["male_pin_diameter_mm"],
            "height_mm": stand["male_pin_height_mm"],
            "pcd_mm": params.m4_pcd_mm,
            "local_centers_mm": male_positions,
        },
        "female_holes": {
            "count": stand["female_hole_count"],
            "diameters_mm": female_diameters,
            "depth_mm": stand["female_hole_depth_mm"],
            "pcd_mm": params.locator_pcd_mm,
            "local_centers_mm": female_positions,
        },
        "fpc_groove": {
            "width_mm": stand["fpc_groove_width_mm"],
            "depth_mm": stand["fpc_groove_depth_mm"],
            "v_range_mm": [
                params.fpc_groove_v_min_mm,
                params.fpc_groove_v_max_mm,
            ],
        },
        "c_ring": {
            "inner_diameter_mm": stand["c_ring_inner_diameter_mm"],
            "outer_diameter_mm": stand["c_ring_outer_diameter_mm"],
            "height_mm": stand["c_ring_height_mm"],
            "opening_width_mm": stand["c_ring_opening_width_mm"],
        },
        "display_orientation": {
            "mechanical_rotation_clockwise_deg": stand[
                "mating_pattern_rotation_clockwise_deg"
            ],
            "six_oclock_pin_index": stand[
                "display_six_oclock_pin_index"
            ],
            "six_oclock_pin_local_mm": male_positions[
                stand["display_six_oclock_pin_index"] - 1
            ],
            "nominal_firmware_rotation_deg": stand[
                "display_rotation_nominal_deg"
            ],
        },
        "measured_geometry_source": "exported_stl",
        "measured_geometry": measured_geometry,
        "uses_screws": stand["uses_screws"],
    }


def validate_report(report: dict[str, object]) -> list[str]:
    params = StandParameters()
    errors: list[str] = []

    def check_close(
        actual: float,
        expected: float,
        tolerance: float,
        label: str,
    ) -> None:
        if not math.isclose(actual, expected, abs_tol=tolerance):
            errors.append(f"{label} is {actual}, expected {expected}")

    def check_point(
        actual: list[float] | tuple[float, ...],
        expected: list[float] | tuple[float, ...],
        tolerance: float,
        label: str,
    ) -> None:
        if len(actual) != len(expected) or math.dist(actual, expected) > tolerance:
            errors.append(f"{label} is {actual}, expected {expected}")

    stand_dimensions = report["stand_bounds"]["dimensions_mm"]
    stl = report["stl_bounds"]
    stl_dimensions = stl["dimensions_mm"]

    check_close(stand_dimensions[0], params.base_width_mm, 0.1, "stand width")
    check_close(stand_dimensions[1], params.base_depth_mm, 0.1, "stand depth")
    if not 40.0 < stand_dimensions[2] < 55.0:
        errors.append(f"stand height is outside 40-55 mm: {stand_dimensions[2]}")
    for axis, (scene_value, stl_value) in enumerate(
        zip(stand_dimensions, stl_dimensions)
    ):
        if not math.isclose(scene_value, stl_value, abs_tol=0.05):
            errors.append(
                f"STL axis {axis} differs from scene: {stl_value} vs {scene_value}"
            )
    if stl["mesh_component_count"] != 1:
        errors.append(
            f"exported STL has {stl['mesh_component_count']} connected components"
        )
    if stl["non_manifold_edge_count"] != 0:
        errors.append(
            f"exported STL has {stl['non_manifold_edge_count']} non-manifold edges"
        )
    if stl["degenerate_triangle_count"] != 0:
        errors.append(
            f"exported STL has {stl['degenerate_triangle_count']} degenerate triangles"
        )
    if report["mesh_component_count"] != 1:
        errors.append(
            f"print mesh has {report['mesh_component_count']} connected components"
        )
    if report["non_manifold_edge_count"] != 0:
        errors.append(
            f"print mesh has {report['non_manifold_edge_count']} non-manifold edges"
        )
    if report["module_clearance_mm"] < 5.0:
        errors.append(
            f"module clearance is below 5 mm: {report['module_clearance_mm']}"
        )
    if report["module_clearance_above_base_mm"] < 1.0:
        errors.append(
            "worst-case module envelope is less than 1 mm above the base top: "
            f"{report['module_clearance_above_base_mm']}"
        )
    if report["uses_screws"]:
        errors.append("test stand must not contain screw retention")

    units = report["units"]
    if units["system"] != "METRIC" or units["length_unit"] != "MILLIMETERS":
        errors.append(f"scene units are not millimeters: {units}")

    check_close(
        report["mating_angle_deg"], params.angle_deg, 0.01, "declared mating angle"
    )
    check_point(
        report["mating_center_mm"],
        params.mating_center_mm,
        0.01,
        "declared mating center",
    )
    male_positions = report["male_pins"]["local_centers_mm"]
    expected_male = m4_pin_local_positions(params)
    if report["male_pins"]["count"] != 3 or len(male_positions) != 3:
        errors.append(
            "declared male pin count is not 3: "
            f"{report['male_pins']['count']} / {len(male_positions)}"
        )
    else:
        for actual, expected in zip(male_positions, expected_male):
            check_point(actual, expected, 0.01, "declared male pin center")
    check_close(
        report["male_pins"]["diameter_mm"],
        params.printed_pin_diameter_mm,
        0.01,
        "declared male pin diameter",
    )
    check_close(
        report["male_pins"]["height_mm"],
        params.printed_pin_height_mm,
        0.01,
        "declared male pin height",
    )
    check_close(
        report["male_pins"]["pcd_mm"], params.m4_pcd_mm, 0.01, "declared M4 PCD"
    )

    female_positions = report["female_holes"]["local_centers_mm"]
    expected_female = locator_hole_local_positions(params)
    if report["female_holes"]["count"] != 2 or len(female_positions) != 2:
        errors.append(
            "declared female hole count is not 2: "
            f"{report['female_holes']['count']} / {len(female_positions)}"
        )
    else:
        for actual, expected in zip(female_positions, expected_female):
            check_point(actual, expected, 0.01, "declared female hole center")
        if not math.isclose(
            math.dist(female_positions[0], female_positions[1]),
            20.0,
            abs_tol=0.01,
        ):
            errors.append("female hole center distance is not 20 mm")
    declared_female_diameters = report["female_holes"]["diameters_mm"]
    if len(declared_female_diameters) != len(params.locator_hole_diameters_mm):
        errors.append(
            "declared female hole diameter count is not 2: "
            f"{len(declared_female_diameters)}"
        )
    else:
        for index, (actual, expected) in enumerate(
            zip(declared_female_diameters, params.locator_hole_diameters_mm),
            start=1,
        ):
            check_close(
                actual,
                expected,
                0.01,
                f"declared female hole {index} diameter",
            )
    check_close(
        report["female_holes"]["depth_mm"],
        params.locator_hole_depth_mm,
        0.01,
        "declared female hole depth",
    )
    check_close(
        report["female_holes"]["pcd_mm"],
        params.locator_pcd_mm,
        0.01,
        "declared locator PCD",
    )

    check_close(
        report["fpc_groove"]["width_mm"],
        params.fpc_groove_width_mm,
        0.01,
        "declared FPC groove width",
    )
    check_close(
        report["fpc_groove"]["depth_mm"],
        params.fpc_groove_depth_mm,
        0.01,
        "declared FPC groove depth",
    )

    declared_ring = report["c_ring"]
    check_close(
        declared_ring["inner_diameter_mm"],
        params.c_ring_inner_diameter_mm,
        0.01,
        "declared C-ring inner diameter",
    )
    check_close(
        declared_ring["outer_diameter_mm"],
        params.c_ring_outer_diameter_mm,
        0.01,
        "declared C-ring outer diameter",
    )
    check_close(
        declared_ring["height_mm"],
        params.c_ring_height_mm,
        0.01,
        "declared C-ring height",
    )
    check_close(
        declared_ring["opening_width_mm"],
        params.fpc_groove_width_mm,
        0.01,
        "declared C-ring opening width",
    )

    orientation = report["display_orientation"]
    check_close(
        orientation["mechanical_rotation_clockwise_deg"],
        params.mating_pattern_rotation_clockwise_deg,
        0.01,
        "mechanical mating-pattern rotation",
    )
    expected_orientation_index = params.display_six_oclock_pin_index
    if orientation["six_oclock_pin_index"] != expected_orientation_index:
        errors.append(
            "display six-o'clock M4 pin index is "
            f"{orientation['six_oclock_pin_index']}, "
            f"expected {expected_orientation_index}"
        )
    check_point(
        orientation["six_oclock_pin_local_mm"],
        expected_male[expected_orientation_index - 1],
        0.01,
        "display six-o'clock M4 pin center",
    )
    check_close(
        orientation["nominal_firmware_rotation_deg"],
        params.display_rotation_nominal_deg,
        0.01,
        "nominal firmware rotation",
    )

    measured = report.get("measured_geometry")
    if not isinstance(measured, dict):
        errors.append("measured mating geometry is missing")
        return errors
    check_close(
        measured["mating_angle_deg"],
        params.angle_deg,
        0.15,
        "measured mating angle",
    )
    for offset in measured["mating_plane_sample_offsets_mm"]:
        check_close(offset, 0.0, 0.05, "measured mating plane offset")

    measured_pins = measured["male_pins"]
    if len(measured_pins) != 3:
        errors.append(f"measured {len(measured_pins)} male pins, expected 3")
    else:
        for index, (pin, expected_center) in enumerate(
            zip(measured_pins, expected_male), start=1
        ):
            check_point(
                pin["center_local_mm"],
                expected_center,
                0.05,
                f"measured male pin {index} center",
            )
            check_close(
                pin["diameter_mm"],
                params.printed_pin_diameter_mm,
                0.12,
                f"measured male pin {index} diameter",
            )
            check_close(
                pin["height_mm"],
                params.printed_pin_height_mm,
                0.12,
                f"measured male pin {index} height",
            )

    measured_holes = measured["female_holes"]
    if len(measured_holes) != 2:
        errors.append(f"measured {len(measured_holes)} female holes, expected 2")
    else:
        for index, (hole, expected_center, expected_diameter) in enumerate(
            zip(
                measured_holes,
                expected_female,
                params.locator_hole_diameters_mm,
            ),
            start=1,
        ):
            check_point(
                hole["center_local_mm"],
                expected_center,
                0.05,
                f"measured female hole {index} center",
            )
            check_close(
                hole["diameter_u_mm"],
                expected_diameter,
                0.12,
                f"measured female hole {index} U diameter",
            )
            check_close(
                hole["diameter_v_mm"],
                expected_diameter,
                0.12,
                f"measured female hole {index} V diameter",
            )
            check_close(
                hole["depth_mm"],
                params.locator_hole_depth_mm,
                0.15,
                f"measured female hole {index} depth",
            )

    measured_groove = measured["fpc_groove"]
    check_close(
        measured_groove["pad_depth_mm"],
        params.fpc_groove_depth_mm,
        0.15,
        "measured pad groove depth",
    )
    check_close(
        measured_groove["wedge_depth_mm"],
        params.fpc_groove_depth_mm,
        0.15,
        "measured wedge groove depth",
    )
    check_close(
        measured_groove["pad_width_mm"],
        params.fpc_groove_width_mm,
        0.15,
        "measured pad groove width",
    )
    check_close(
        measured_groove["wedge_width_mm"],
        params.fpc_groove_width_mm,
        0.15,
        "measured wedge groove width",
    )
    expected_transition_floor = -(
        params.mating_pad_proud_mm + params.fpc_groove_depth_mm / 2.0
    )
    check_close(
        measured_groove["transition_floor_normal_mm"],
        expected_transition_floor,
        0.15,
        "measured groove transition floor",
    )

    measured_ring = measured["c_ring"]
    check_close(
        measured_ring["inner_diameter_mm"],
        params.c_ring_inner_diameter_mm,
        0.12,
        "measured C-ring inner diameter",
    )
    check_close(
        measured_ring["outer_diameter_mm"],
        params.c_ring_outer_diameter_mm,
        0.12,
        "measured C-ring outer diameter",
    )
    check_close(
        measured_ring["height_mm"],
        params.c_ring_height_mm,
        0.12,
        "measured C-ring height",
    )
    check_close(
        measured_ring["opening_width_mm"],
        params.fpc_groove_width_mm,
        0.12,
        "measured C-ring opening width",
    )
    opening_samples = measured_ring.get("opening_width_samples_mm")
    expected_sample_heights = (
        0.05,
        params.c_ring_height_mm / 2.0,
        params.c_ring_height_mm - 0.05,
    )
    if not isinstance(opening_samples, list) or len(opening_samples) != 3:
        errors.append(
            "measured C-ring opening must have bottom, middle, and top samples"
        )
    else:
        for index, (sample, expected_height) in enumerate(
            zip(opening_samples, expected_sample_heights), start=1
        ):
            if not isinstance(sample, dict):
                errors.append(f"measured C-ring opening sample {index} is invalid")
                continue
            check_close(
                sample.get("normal_mm", math.nan),
                expected_height,
                0.01,
                f"measured C-ring opening sample {index} height",
            )
            check_close(
                sample.get("width_mm", math.nan),
                params.fpc_groove_width_mm,
                0.12,
                f"measured C-ring opening width sample {index}",
            )
    return errors


def write_reports(report: dict[str, object]) -> tuple[Path, Path]:
    report_dir = ROOT_DIR / "outputs" / "reports"
    report_dir.mkdir(parents=True, exist_ok=True)
    json_path = report_dir / "dimensions.json"
    text_path = report_dir / "dimensions.txt"
    json_path.write_text(
        json.dumps(report, indent=2, sort_keys=True), encoding="utf-8"
    )
    stand_dimensions = report["stand_bounds"]["dimensions_mm"]
    lines = [
        "ESP32-S3 Round Display Test Stand",
        "",
        f"Stand bounds (mm): {stand_dimensions}",
        f"STL triangles: {report['stl_bounds']['triangle_count']}",
        f"STL connected components: {report['stl_bounds']['mesh_component_count']}",
        f"STL non-manifold edges: {report['stl_bounds']['non_manifold_edge_count']}",
        f"STL degenerate triangles: {report['stl_bounds']['degenerate_triangle_count']}",
        f"BLEND connected components: {report['mesh_component_count']}",
        f"BLEND non-manifold edges: {report['non_manifold_edge_count']}",
        f"Mating angle (deg): {report['mating_angle_deg']}",
        f"Conservative module clearance (mm): {report['module_clearance_mm']}",
        f"Male pins: {report['male_pins']}",
        f"Female holes: {report['female_holes']}",
        f"C-ring: {report['c_ring']}",
        f"Measured geometry source: {report['measured_geometry_source']}",
        f"Measured C-ring: {report['measured_geometry']['c_ring']}",
        f"FPC groove: {report['fpc_groove']}",
        f"Display orientation: {report['display_orientation']}",
        f"Uses screws: {report['uses_screws']}",
    ]
    text_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return json_path, text_path


def main() -> None:
    stl_path = ROOT_DIR / "outputs" / "models" / "round_display_test_stand.stl"
    stand = bpy.data.objects.get("STAND_PRINT")
    if stand is None:
        raise SystemExit("VERIFICATION FAIL: STAND_PRINT is missing")
    report = collect_report(StandParameters(), stand, stl_path)
    errors = validate_report(report)
    json_path, text_path = write_reports(report)
    print(json.dumps(report, indent=2, sort_keys=True))
    print(f"JSON_REPORT={json_path}")
    print(f"TEXT_REPORT={text_path}")
    if errors:
        for error in errors:
            print(f"ERROR={error}")
        raise SystemExit(1)
    print("VERIFICATION PASS")


if __name__ == "__main__":
    main()
