#!/bin/bash
sleep 1s;

gnome-terminal --tab --title="ROSCORE" --command "bash -c \"source ~/.bashrc; killall gzclient && killall gzserver; roscore; exec bash\"";
sleep 1s;
gnome-terminal --tab --title="Gazebo Sim" --command "bash -c \"source ~/.bashrc; roslaunch swarm2_launch multi_dingo_sim_with_rviz_and_ez_publisher_anchor.launch; exec bash\"";
# sleep 10s;
# gnome-terminal --tab --title="High Level Viz" --command "bash -c \"source ~/.bashrc; roslaunch high_level_viz high_level_viz.launch; exec bash\"";




