# [setup]
import math
import os

import magnum as mn
import numpy as np
from matplotlib import pyplot as plt

import habitat_sim
from habitat_utils.common import quat_from_angle_axis

dir_path = os.path.dirname(os.path.realpath(__file__))
data_path = os.path.join(dir_path, "../../data")
output_path = os.path.join(dir_path, "semantic_object_tutorial_output/")

save_index = 0


def show_img(data, save):
    # display rgb and semantic images side-by-side
    fig = plt.figure(figsize=(12, 12))
    ax1 = fig.add_subplot(1, 2, 1)
    ax1.axis("off")
    ax1.imshow(data[0], interpolation="nearest")
    ax2 = fig.add_subplot(1, 2, 2)
    ax2.axis("off")
    ax2.imshow(data[1], interpolation="nearest")
    plt.axis("off")
    plt.show(block=False)
    if save:
        global save_index
        plt.savefig(
            output_path + str(save_index) + ".jpg",
            bbox_inches="tight",
            pad_inches=0,
            quality=50,
        )
        save_index += 1
    plt.pause(1)


def get_obs(sim, show, save):
    # render sensor ouputs and optionally show them
    rgb_obs = sim.get_sensor_observations()["rgba_camera"]
    semantic_obs = sim.get_sensor_observations()["semantic_camera"]
    if show:
        show_img((rgb_obs, semantic_obs), save)


def place_agent(sim):
    # place our agent in the scene
    agent_state = habitat_sim.AgentState()
    agent_state.position = [5.0, 0.0, 1.0]
    agent_state.rotation = quat_from_angle_axis(
        math.radians(70), np.array([0, 1.0, 0])
    ) * quat_from_angle_axis(math.radians(-20), np.array([1.0, 0, 0]))
    agent = sim.initialize_agent(0, agent_state)
    return agent.scene_node.transformation_matrix()


def make_configuration(scene_file):
    # simulator configuration
    backend_cfg = habitat_sim.SimulatorConfiguration()
    backend_cfg.scene.id = scene_file
    backend_cfg.enable_physics = True

    # sensor configurations
    # Note: all sensors must have the same resolution
    # setup rgb and semantic sensors
    camera_resolution = [1080, 960]
    sensors = {
        "rgba_camera": {
            "sensor_type": habitat_sim.SensorType.COLOR,
            "resolution": camera_resolution,
            "position": [0.0, 1.5, 0.0],  # ::: fix y to be 0 later
        },
        "semantic_camera": {
            "sensor_type": habitat_sim.SensorType.SEMANTIC,
            "resolution": camera_resolution,
            "position": [0.0, 1.5, 0.0],
        },
    }

    sensor_specs = []
    for sensor_uuid, sensor_params in sensors.items():
        sensor_spec = habitat_sim.SensorSpec()
        sensor_spec.uuid = sensor_uuid
        sensor_spec.sensor_type = sensor_params["sensor_type"]
        sensor_spec.resolution = sensor_params["resolution"]
        sensor_spec.position = sensor_params["position"]
        sensor_specs.append(sensor_spec)

    # agent configuration
    agent_cfg = habitat_sim.agent.AgentConfiguration()
    agent_cfg.sensor_specifications = sensor_specs

    return habitat_sim.Configuration(backend_cfg, [agent_cfg])


# [/setup]

# This is wrapped such that it can be added to a unit test
def main(show_imgs=True, save_imgs=False):
    if save_imgs:
        if not os.path.exists(output_path):
            os.mkdir(output_path)

    # [semantic id]

    # create the simulator and render flat shaded scene
    cfg = make_configuration(scene_file="NONE")
    sim = habitat_sim.Simulator(cfg)

    test_scenes = [
        "data/scene_datasets/habitat-test-scenes/apartment_1.glb",
        "data/scene_datasets/habitat-test-scenes/van-gogh-room.glb",
    ]

    for scene in test_scenes:
        # reconfigure the simulator with a new scene asset
        cfg = make_configuration(scene_file=scene)
        sim.reconfigure(cfg)
        agent_transform = place_agent(sim)  # noqa: F841
        get_obs(sim, show_imgs, save_imgs)

        # get the physics object attributes manager
        obj_templates_mgr = sim.get_object_template_manager()

        # load some chair object template from configuration file
        chair_template_id = obj_templates_mgr.load_configs(
            str(os.path.join(data_path, "test_assets/objects/chair"))
        )[0]

        # add 2 chairs with default semanticId == 0 and arrange them
        chair_ids = []
        chair_ids.append(sim.add_object(chair_template_id))
        chair_ids.append(sim.add_object(chair_template_id))

        sim.set_rotation(
            mn.Quaternion.rotation(mn.Deg(-115), mn.Vector3.y_axis()), chair_ids[0]
        )
        sim.set_translation([2.0, 0.47, 0.9], chair_ids[0])

        sim.set_translation([2.9, 0.47, 0.0], chair_ids[1])
        get_obs(sim, show_imgs, save_imgs)

        # set the semanticId for both chairs
        sim.set_object_semantic_id(2, chair_ids[0])
        sim.set_object_semantic_id(2, chair_ids[1])
        get_obs(sim, show_imgs, save_imgs)

        # set the semanticId for one chair
        sim.set_object_semantic_id(1, chair_ids[1])
        get_obs(sim, show_imgs, save_imgs)

        # add a box with default semanticId configured in the template
        box_template = habitat_sim.attributes.ObjectAttributes()
        box_template.render_asset_handle = str(
            os.path.join(data_path, "test_assets/objects/transform_box.glb")
        )

        box_template.scale = np.array([0.2, 0.2, 0.2])
        # set the default semantic id for this object template
        box_template.semantic_id = 10
        obj_templates_mgr = sim.get_object_template_manager()
        box_template_id = obj_templates_mgr.register_template(box_template, "box")
        box_id = sim.add_object(box_template_id)
        sim.set_translation([3.5, 0.47, 0.9], box_id)
        sim.set_rotation(
            mn.Quaternion.rotation(mn.Deg(-30), mn.Vector3.y_axis()), box_id
        )

        get_obs(sim, show_imgs, save_imgs)

        # set semantic id for specific SceneNode components of the box object
        box_visual_nodes = sim.get_object_visual_scene_nodes(box_id)
        box_visual_nodes[6].semantic_id = 3
        box_visual_nodes[7].semantic_id = 4
        get_obs(sim, show_imgs, save_imgs)

    # [/semantic id]


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--no-show-images", dest="show_images", action="store_false")
    parser.add_argument("--no-save-images", dest="save_images", action="store_false")
    parser.set_defaults(show_images=True, save_images=True)
    args = parser.parse_args()
    main(show_imgs=args.show_images, save_imgs=args.save_images)
