from pathlib import Path

import bpy
from mathutils import Vector


def remove_render_collection() -> None:
    collection = bpy.data.collections.get("RENDER_SETUP")
    if collection is None:
        return
    for obj in list(collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(collection)


def create_render_collection() -> bpy.types.Collection:
    collection = bpy.data.collections.new("RENDER_SETUP")
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(
    obj: bpy.types.Object, collection: bpy.types.Collection
) -> None:
    for current in list(obj.users_collection):
        current.objects.unlink(obj)
    collection.objects.link(obj)


def principled_node(material: bpy.types.Material):
    return next(
        (
            node
            for node in material.node_tree.nodes
            if node.type == "BSDF_PRINCIPLED"
        ),
        None,
    )


def tune_material(
    name: str,
    color: tuple[float, float, float, float],
    metallic: float,
    roughness: float,
) -> None:
    material = bpy.data.materials.get(name)
    if material is None:
        return
    material.diffuse_color = color
    node = principled_node(material)
    if node is not None:
        node.inputs["Base Color"].default_value = color
        node.inputs["Metallic"].default_value = metallic
        node.inputs["Roughness"].default_value = roughness
        node.inputs["Alpha"].default_value = color[3]


def new_material(
    name: str,
    color: tuple[float, float, float, float],
    roughness: float,
) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.diffuse_color = color
    node = principled_node(material)
    if node is not None:
        node.inputs["Base Color"].default_value = color
        node.inputs["Roughness"].default_value = roughness
    return material


def look_at(obj: bpy.types.Object, target: Vector) -> None:
    direction = target - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def add_area_light(
    name: str,
    location: tuple[float, float, float],
    target: Vector,
    energy: float,
    size: float,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    data = bpy.data.lights.new(name, type="AREA")
    data.energy = energy
    data.shape = "DISK"
    data.size = size
    light = bpy.data.objects.new(name, data)
    light.location = location
    collection.objects.link(light)
    look_at(light, target)
    return light


def configure_world() -> None:
    world = bpy.context.scene.world
    if world is None:
        world = bpy.data.worlds.new("Render World")
        bpy.context.scene.world = world
    background = next(
        (node for node in world.node_tree.nodes if node.type == "BACKGROUND"),
        None,
    )
    if background is not None:
        background.inputs["Color"].default_value = (0.07, 0.09, 0.13, 1.0)
        background.inputs["Strength"].default_value = 0.55


def configure_camera(collection: bpy.types.Collection) -> bpy.types.Object:
    camera_data = bpy.data.cameras.new("Preview Camera")
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 135.0
    camera_data.lens = 55.0
    camera = bpy.data.objects.new("Preview Camera", camera_data)
    camera.location = (118.0, -150.0, 105.0)
    collection.objects.link(camera)
    look_at(camera, Vector((0.0, 3.0, 21.0)))
    bpy.context.scene.camera = camera
    return camera


def create_ground(collection: bpy.types.Collection) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, -1.25))
    ground = bpy.context.object
    ground.name = "PREVIEW_GROUND"
    ground.dimensions = (190.0, 170.0, 2.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    move_to_collection(ground, collection)
    material = new_material(
        "PREVIEW_GROUND_MATERIAL", (0.19, 0.22, 0.27, 1.0), 0.62
    )
    ground.data.materials.append(material)
    return ground


def configure_scene() -> None:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.film_transparent = False
    scene.render.image_settings.compression = 35
    scene.view_settings.exposure = 1.0
    configure_world()
    tune_material("STAND_MATERIAL", (0.12, 0.19, 0.27, 1.0), 0.18, 0.3)
    tune_material(
        "MODULE_REFERENCE_MATERIAL", (0.1, 0.13, 0.17, 1.0), 0.35, 0.24
    )
    tune_material(
        "SCREEN_REFERENCE_MATERIAL", (0.01, 0.42, 0.55, 1.0), 0.1, 0.16
    )
    tune_material(
        "FPC_REFERENCE_MATERIAL", (0.95, 0.22, 0.025, 1.0), 0.0, 0.5
    )


def render_previews(root_dir: Path) -> dict[str, Path]:
    stand = bpy.data.objects.get("STAND_PRINT")
    reference = bpy.data.collections.get("REFERENCE")
    if stand is None or reference is None:
        raise RuntimeError("verified stand scene is not loaded")

    configure_scene()
    remove_render_collection()
    render_collection = create_render_collection()
    create_ground(render_collection)
    target = Vector((0.0, 3.0, 21.0))
    camera = configure_camera(render_collection)
    add_area_light(
        "KEY_LIGHT", (85.0, -105.0, 145.0), target, 90000.0, 72.0, render_collection
    )
    add_area_light(
        "FILL_LIGHT", (-110.0, -40.0, 75.0), target, 45000.0, 65.0, render_collection
    )
    add_area_light(
        "RIM_LIGHT", (25.0, 115.0, 125.0), target, 65000.0, 58.0, render_collection
    )

    output_dir = root_dir / "outputs" / "renders"
    output_dir.mkdir(parents=True, exist_ok=True)
    print_path = output_dir / "stand_print.png"
    assembled_path = output_dir / "stand_assembled.png"

    reference.hide_render = True
    camera.data.ortho_scale = 122.0
    camera.location = (105.0, -135.0, 105.0)
    look_at(camera, Vector((0.0, 2.0, 20.0)))
    bpy.context.scene.render.filepath = str(print_path)
    bpy.ops.render.render(write_still=True)

    reference.hide_render = False
    camera.data.ortho_scale = 145.0
    camera.location = (125.0, -170.0, 118.0)
    look_at(camera, Vector((0.0, 0.0, 30.0)))
    bpy.context.scene.render.filepath = str(assembled_path)
    bpy.ops.render.render(write_still=True)

    return {"print": print_path, "assembled": assembled_path}


def main() -> None:
    root_dir = Path(__file__).resolve().parents[1]
    paths = render_previews(root_dir)
    for label, path in paths.items():
        print(f"RENDER_{label.upper()}={path}")


if __name__ == "__main__":
    main()
