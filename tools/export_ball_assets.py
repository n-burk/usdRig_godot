#!/usr/bin/env python3
"""Export the tutorial's composed USD mesh, UVs and bound material for Godot.

This deliberately validates the ball's supported material graph, rather than
silently approximating an arbitrary USD asset. USD/OpenSubdiv are build-only.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import base64
import struct

PLUGIN = Path(__file__).resolve().parents[1]
USD = PLUGIN.parent / "usd-install"
RIG = PLUGIN.parent / "usdRig"
DLL_HANDLES = []
if os.name == "nt":
    for folder in (USD / "bin", USD / "lib"):
        DLL_HANDLES.append(os.add_dll_directory(str(folder)))
for folder in (USD / "Lib/site-packages", USD / "lib/python"):
    if folder.exists():
        sys.path.insert(0, str(folder))
# An editable NanoUSD install can claim `pxr` ahead of sys.path. Require
# the real sibling OpenUSD bindings, not an API-compatible substitute.
for finder in list(sys.meta_path):
    try:
        spec = finder.find_spec("pxr", None)
    except (AttributeError, ImportError):
        continue
    if spec and str(USD).lower() not in ((spec.origin or "") + str(spec.submodule_search_locations)).lower():
        sys.meta_path.remove(finder)
from pxr import Gf, Usd, UsdGeom, UsdShade


def main():
    stage_path = RIG / "docs/examples/tutorial_rolling_ball_free.usda"
    stage = Usd.Stage.Open(str(stage_path))
    mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/BallAsset/Geom/Ball"))
    assert mesh.GetSubdivisionSchemeAttr().Get() == "catmullClark"
    assert mesh.GetOrientationAttr().Get() == "rightHanded"
    assert not mesh.GetCreaseIndicesAttr().Get() and not mesh.GetHoleIndicesAttr().Get()
    st = UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st")
    assert st.GetInterpolation() == "faceVarying"
    points = mesh.GetPointsAttr().Get()
    counts = list(mesh.GetFaceVertexCountsAttr().Get())
    indices = list(mesh.GetFaceVertexIndicesAttr().Get())
    corner_uv = st.ComputeFlattened()
    assert sum(counts) == len(indices) == len(corner_uv)
    # Match Hydra's face-varying topology exactly: unindexed primvars have
    # one independent value per corner. Equal values must not be welded.
    uv = [tuple(value) for value in st.Get()]
    uv_indices = list(st.GetIndices()) if st.IsIndexed() else list(range(len(uv)))
    # This tutorial's sphere is wound inward. usdview shows it with culling
    # off; Godot culls back faces. Reverse each face together with its UV
    # corners, preserving the surface and texture placement on the outside.
    offset = 0
    reversed_faces = 0
    for count in counts:
        face = indices[offset:offset + count]
        a, b, c = (points[i] for i in face[:3])
        if Gf.Dot(Gf.Cross(b - a, c - a), (a + b + c) / 3 - Gf.Vec3f(0, 1, 0)) < 0:
            indices[offset:offset + count] = reversed(face)
            uv_indices[offset:offset + count] = reversed(uv_indices[offset:offset + count])
            reversed_faces += 1
        offset += count
    rule = mesh.GetPrim().GetAttribute("faceVaryingLinearInterpolation").Get()
    rules = ["none", "cornersOnly", "cornersPlus1", "cornersPlus2", "boundaries", "all"]
    material, _ = UsdShade.MaterialBindingAPI(mesh).ComputeBoundMaterial()
    surface, _, _ = material.ComputeSurfaceSource()
    assert surface.GetIdAttr().Get() == "UsdPreviewSurface"
    inputs = {}
    for name in ("diffuseColor", "emissiveColor"):
        source, _, _ = surface.GetInput(name).GetConnectedSource()
        shader = UsdShade.Shader(source.GetPrim())
        assert shader.GetIdAttr().Get() == "UsdUVTexture"
        reader, _, _ = shader.GetInput("st").GetConnectedSource()
        assert UsdShade.Shader(reader.GetPrim()).GetInput("varname").Get() == "st"
        assert shader.GetInput("wrapS").Get() == "repeat"
        assert shader.GetInput("wrapT").Get() == "clamp"
        asset = shader.GetInput("file").Get()
        inputs[name] = dict(texture=asset.resolvedPath, scale=list(shader.GetInput("scale").Get()))
    assert inputs["diffuseColor"]["texture"] == inputs["emissiveColor"]["texture"]
    assets = PLUGIN / "build/rolling_ball"
    assets.mkdir(parents=True, exist_ok=True)
    source_mesh = assets / "tutorial_ball.control_mesh.txt"
    with source_mesh.open("w") as stream:
        stream.write(f"{len(points)} {len(counts)} {len(uv)} {rules.index(rule)}\n")
        for p in points:
            stream.write(" ".join(map(str, p)) + "\n")
        stream.write(" ".join(map(str, counts)) + "\n")
        stream.write(" ".join(map(str, indices)) + "\n")
        for v in uv:
            stream.write(" ".join(map(str, v)) + "\n")
        stream.write(" ".join(map(str, uv_indices)) + "\n")
    tool = PLUGIN / "tools" / ("ball_subdivide.exe" if os.name == "nt" else "ball_subdivide")
    cpp = tool.with_suffix(".cpp")
    if not tool.exists() or tool.stat().st_mtime < cpp.stat().st_mtime:
        if os.name == "nt":
            subprocess.run([str(PLUGIN / "tools/build_ball_subdivide.bat")], check=True)
        else:
            subprocess.run(["c++", "-std=c++17", "-O2", str(cpp), "-I" + str(USD / "include"),
                            "-L" + str(USD / "lib"), "-losdCPU", "-o", str(tool)], check=True)
    subprocess.run([str(tool), str(source_mesh), str(assets / "tutorial_ball.obj"),
                    str(assets / "tutorial_ball.stencils.json")], check=True)
    texture = Path(inputs["diffuseColor"]["texture"])
    shutil.copy2(texture, assets / texture.name)
    params = {name: surface.GetInput(name).Get() for name in ("metallic", "roughness", "specular")}
    diffuse = inputs["diffuseColor"]["scale"]
    emission = inputs["emissiveColor"]["scale"]
    material_text = '''[gd_resource type="ShaderMaterial" load_steps=3 format=3]

[ext_resource type="Shader" path="res://ball_assets/tutorial_ball.gdshader" id="1"]
[ext_resource type="Texture2D" path="res://ball_assets/pixar_ball.png" id="2"]

[resource]
resource_name = "BallMaterial (USD Preview Surface)"
shader = ExtResource("1")
shader_parameter/ball_texture = ExtResource("2")
'''
    material_text += f'shader_parameter/diffuse_scale = Vector3({", ".join(map(str, diffuse[:3]))})\n'
    material_text += f'shader_parameter/emission_scale = Vector3({", ".join(map(str, emission[:3]))})\n'
    for name, value in params.items():
        material_text += f"shader_parameter/{name} = {value}\n"
    (assets / "tutorial_ball_material.tres").write_text(material_text)
    manifest = dict(source="usdRig/docs/examples/tutorial_rolling_ball_free.usda", prim=str(mesh.GetPath()),
                    material=str(material.GetPath()), points=len(points), faces=len(counts),
                    corners=len(indices), uv_interpolation=str(st.GetInterpolation()),
                    outward_winding_corrected_faces=reversed_faces,
                    fvar_rule=rule, subdivision="OpenSubdiv Catmull-Clark, level 2, limit surface",
                    texture_sha256=hashlib.sha256(texture.read_bytes()).hexdigest(),
                    diffuse_scale=diffuse, emission_scale=emission, **params)
    (assets / "source_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Presentation is an optional, versioned section of the same REXB file.
    # OBJ/material files above are bake intermediates; none is loaded by Godot.
    vertices, normals, texcoords, faces = [], [], [], []
    for line in (assets / "tutorial_ball.obj").read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v": vertices.append(list(map(float, fields[1:])))
        elif fields[0] == "vn": normals.append(list(map(float, fields[1:])))
        elif fields[0] == "vt": texcoords.append(list(map(float, fields[1:])))
        elif fields[0] == "f": faces.append([tuple(int(x)-1 for x in c.split('/')) for c in fields[1:]])
    render_vertices, render_uvs = [], []
    for face in faces:
        for j in range(1, len(face)-1):
            # Godot front faces are clockwise. USD/OBJ are counterclockwise.
            for vi, ti, _ in (face[0], face[j+1], face[j]):
                render_vertices.append(vi)
                render_uvs.extend((texcoords[ti][0], 1-texcoords[ti][1]))
    controls = []
    for prim in stage.Traverse():
        exposed = prim.GetAttribute("rigExec:exposedAvars").Get()
        if not exposed:
            continue
        assert str(prim.GetTypeName()) == "RigExecControl"
        public = prim.GetAttribute("rigExec:publicName").Get() or prim.GetName()
        for channel in exposed:
            assert channel in ("tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz")
            attr = prim.GetAttribute("avars:" + channel)
            controls.append(dict(name=public + "." + channel, path=str(attr.GetPath()),
                                 default=attr.Get(Usd.TimeCode(1001)),
                                 unit="degrees" if channel.startswith('r') else
                                 "asset_units" if channel.startswith('t') else "ratio"))
    assert controls and len({c['name'] for c in controls}) == len(controls)
    presentation = dict(version=1, controls=controls, meshes=[dict(
        path=str(mesh.GetPath()), points_path=str(mesh.GetPointsAttr().GetPath()),
        source_point_count=len(points),
        stencils=json.loads((assets / "tutorial_ball.stencils.json").read_text()),
        vertex_indices=render_vertices, uvs=render_uvs,
        material=dict(model="UsdPreviewSurface", texture_png=base64.b64encode(texture.read_bytes()).decode('ascii'),
                      diffuse_scale=diffuse[:3], emission_scale=emission[:3],
                      wrap_s="repeat", wrap_t="clamp", **params))], source=manifest)
    target = PLUGIN / "demo/rolling_ball.rigexec"
    data = target.read_bytes()
    magic, version, count, flags = struct.unpack_from('<4sIII', data)
    assert magic == b'REXB' and version & 65535 == 1
    sections = []
    for i in range(count):
        tag, offset, size = struct.unpack_from('<IQQ', data, 16+i*20)
        if tag != 13:
            sections.append((tag, data[offset:offset+size]))
    sections.append((13, json.dumps(presentation, separators=(',', ':'), allow_nan=False).encode('utf-8')))
    offset = 16 + len(sections)*20
    table = bytearray()
    for tag, payload in sections:
        table.extend(struct.pack('<IQQ', tag, offset, len(payload)))
        offset += len(payload)
    result = struct.pack('<4sIII', magic, version, len(sections), flags) + table + b''.join(p for _, p in sections)
    target.write_bytes(result)
    print(f"Embedded mesh, subdivision stencils, material, PNG and {len(controls)} public controls in {target.name}")
    print(f"Exported {len(points)} source points, {len(counts)} faces, original st UVs and {material.GetPath()}")


if __name__ == "__main__":
    main()
