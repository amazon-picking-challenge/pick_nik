<?xml version="1.0" encoding="utf-8"?>
<launch>

  <!-- GDB Debug Option -->
  <arg name="debug" default="false" />
  <arg unless="$(arg debug)" name="launch_prefix" value="" />
  <arg     if="$(arg debug)" name="launch_prefix" 
	   value="gdb -x $(find picknik_main)/launch/debug_settings.gdb --ex run --args" />

  <group ns="perception">

    <!-- Main process -->
    <node name="fake_perception_server" pkg="picknik_main" type="fake_perception_server" respawn="true" 
	  launch-prefix="$(arg launch_prefix)" output="screen" />

  </group>

</launch>