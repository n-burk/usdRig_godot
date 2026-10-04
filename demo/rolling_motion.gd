class_name RollingMotion
extends RefCounted

# Orientation has history. Adding Euler angles from absolute X/Z cannot
# describe successive rolls about different axes (rotations do not commute).
static func advance(orientation: Quaternion, displacement: Vector3,
		normal: Vector3, radius: float) -> Quaternion:
	if radius <= 0.0 or normal.length_squared() < 0.000001:
		return orientation
	var n := normal.normalized()
	var tangent := displacement - n * displacement.dot(n)
	if tangent.length_squared() < 0.00000001:
		return orientation
	var axis := n.cross(tangent).normalized()
	return (Quaternion(axis, tangent.length() / radius) * orientation).normalized()

static func rig_degrees(orientation: Quaternion) -> Vector3:
	# USD's row-vector XYZ is Godot's column-vector ZYX. Euler values are
	# only the interface to the rig, never the accumulated rolling state.
	return Basis(orientation).get_euler(EULER_ORDER_ZYX) * (180.0 / PI)
