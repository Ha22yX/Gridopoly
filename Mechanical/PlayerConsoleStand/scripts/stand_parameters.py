from dataclasses import dataclass
import math


@dataclass(frozen=True)
class StandParameters:
    angle_deg: float = 35.0
    base_width_mm: float = 94.0
    base_depth_mm: float = 82.0
    base_thickness_mm: float = 4.0
    base_corner_radius_mm: float = 5.0
    mating_center_mm: tuple[float, float, float] = (0.0, 3.0, 28.0)
    module_outer_diameter_mm: float = 80.13
    module_rear_interface_diameter_mm: float = 34.0
    module_rear_interface_depth_mm: float = 7.6
    wedge_width_mm: float = 54.0
    wedge_y_front_mm: float = -18.0
    wedge_y_rear_mm: float = 32.0
    mating_pad_diameter_mm: float = 40.0
    mating_pad_proud_mm: float = 2.0
    c_ring_inner_diameter_mm: float = 34.5
    c_ring_outer_diameter_mm: float = 40.0
    c_ring_height_mm: float = 7.6
    m4_pcd_mm: float = 18.0
    printed_pin_diameter_mm: float = 2.9
    printed_pin_height_mm: float = 3.0
    printed_pin_tip_height_mm: float = 0.5
    locator_pcd_mm: float = 20.0
    locator_post_diameters_mm: tuple[float, float] = (3.0, 4.0)
    locator_hole_diameters_mm: tuple[float, float] = (3.1, 4.1)
    locator_hole_depth_mm: float = 4.0
    mating_pattern_rotation_clockwise_deg: float = 47.5
    display_six_oclock_pin_index: int = 3
    display_rotation_nominal_deg: float = 0.0
    fpc_groove_width_mm: float = 9.0
    fpc_groove_depth_mm: float = 2.0
    fpc_groove_v_min_mm: float = -28.0
    fpc_groove_v_max_mm: float = 2.0
    fpc_groove_transition_v_min_mm: float = -20.0
    fpc_groove_transition_v_max_mm: float = -17.0


def local_basis(
    params: StandParameters,
) -> tuple[
    tuple[float, float, float],
    tuple[float, float, float],
    tuple[float, float, float],
]:
    angle = math.radians(params.angle_deg)
    u_axis = (1.0, 0.0, 0.0)
    v_axis = (0.0, math.cos(angle), math.sin(angle))
    normal = (0.0, -math.sin(angle), math.cos(angle))
    return u_axis, v_axis, normal


def local_to_world(
    x_mm: float,
    v_mm: float,
    normal_mm: float,
    params: StandParameters,
) -> tuple[float, float, float]:
    u_axis, v_axis, normal = local_basis(params)
    center = params.mating_center_mm
    return tuple(
        center[index]
        + x_mm * u_axis[index]
        + v_mm * v_axis[index]
        + normal_mm * normal[index]
        for index in range(3)
    )


def rotate_local_clockwise(
    x_mm: float,
    v_mm: float,
    clockwise_deg: float,
) -> tuple[float, float]:
    angle = math.radians(clockwise_deg)
    cosine = math.cos(angle)
    sine = math.sin(angle)
    return (
        x_mm * cosine + v_mm * sine,
        -x_mm * sine + v_mm * cosine,
    )


def rotate_mating_pattern_local(
    x_mm: float,
    v_mm: float,
    params: StandParameters,
) -> tuple[float, float]:
    return rotate_local_clockwise(
        x_mm,
        v_mm,
        params.mating_pattern_rotation_clockwise_deg,
    )


def m4_pin_local_positions(
    params: StandParameters,
) -> tuple[tuple[float, float], tuple[float, float], tuple[float, float]]:
    radius = params.m4_pcd_mm / 2.0
    side_offset = radius * math.sqrt(3.0) / 2.0
    lower_offset = -radius / 2.0
    unrotated = (
        (0.0, radius),
        (-side_offset, lower_offset),
        (side_offset, lower_offset),
    )
    return tuple(
        rotate_mating_pattern_local(x_mm, v_mm, params)
        for x_mm, v_mm in unrotated
    )


def locator_hole_local_positions(
    params: StandParameters,
) -> tuple[tuple[float, float], tuple[float, float]]:
    radius = params.locator_pcd_mm / 2.0
    return tuple(
        rotate_mating_pattern_local(x_mm, v_mm, params)
        for x_mm, v_mm in ((-radius, 0.0), (radius, 0.0))
    )


def conservative_module_clearance_mm(params: StandParameters) -> float:
    vertical_radius = (
        params.module_outer_diameter_mm
        / 2.0
        * math.sin(math.radians(params.angle_deg))
    )
    return params.mating_center_mm[2] - vertical_radius
