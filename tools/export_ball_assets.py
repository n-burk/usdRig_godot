#!/usr/bin/env python3
"""Export the tutorial's composed USD mesh, UVs and bound material for Godot.

This deliberately validates the ball's supported material graph, rather than
silently approximating an arbitrary USD asset. USD/OpenSubdiv are build-only.
It writes build/rolling_ball/presentation.rexp, the presentation buffer that
`rigExecBake --presentation` embeds in rolling_ball.rigexec.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

try:
    import numpy as np  # FlatBuffers' CreateNumpyVector packs the tables.
except ImportError:
    sys.exit("export_ball_assets: numpy is required (pip install numpy)")

PLUGIN = Path(__file__).resolve().parents[1]
USD = PLUGIN.parent / "usd-install"
RIG = PLUGIN.parent / "usdRig"
# The vendored FlatBuffers runtime shadows any installed copy: the generated
# builder code belongs to this exact release.
sys.path.insert(0, str(PLUGIN / "tools/generated"))
sys.path.insert(0, str(PLUGIN / "tools/thirdparty/flatbuffers/python"))
import flatbuffers
assert flatbuffers.__version__ == "25.12.19", flatbuffers.__version__
from rigExec.fb import (Presentation, PresentationColor, PresentationControl,
                        PresentationMaterial, PresentationMesh, PresentationUnit,
                        PresentationWrap)
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

UNITS = {"t": PresentationUnit.PresentationUnit.AssetUnits,
         "r": PresentationUnit.PresentationUnit.Degrees,
         "s": PresentationUnit.PresentationUnit.Ratio}


def table_vector(builder, start, tables):
    start(builder, len(tables))
    for table in reversed(tables):
        builder.PrependUOffsetTRelative(table)
    return builder.EndVector()


def build_presentation(controls, mesh_path, points_path, point_count, stencils,
                       render_vertices, render_uvs, png, diffuse, emission,
                       params, manifest):
    """The REXP buffer: controls name inputs of the baked file (which holds
    their defaults); stencils are CSR, three groups (p, u, v) per vertex."""
    offsets, indices, weights = [0], [], []
    for record in stencils:
        assert len(record) == 3
        for group in record:
            assert group and len(group) % 2 == 0
            indices.extend(int(i) for i in group[0::2])
            weights.extend(group[1::2])
            offsets.append(len(indices))
    builder = flatbuffers.Builder(0)
    control_tables = []
    for name, input_path, unit in controls:
        name_offset = builder.CreateString(name)
        input_offset = builder.CreateString(input_path)
        PresentationControl.PresentationControlStart(builder)
        PresentationControl.PresentationControlAddName(builder, name_offset)
        PresentationControl.PresentationControlAddInput(builder, input_offset)
        PresentationControl.PresentationControlAddUnit(builder, unit)
        control_tables.append(PresentationControl.PresentationControlEnd(builder))
    controls_vector = table_vector(builder, Presentation.PresentationStartControlsVector, control_tables)
    model = builder.CreateString("UsdPreviewSurface")
    texture_png = builder.CreateByteVector(png)
    PresentationMaterial.PresentationMaterialStart(builder)
    PresentationMaterial.PresentationMaterialAddModel(builder, model)
    PresentationMaterial.PresentationMaterialAddTexturePng(builder, texture_png)
    PresentationMaterial.PresentationMaterialAddDiffuseScale(
        builder, PresentationColor.CreatePresentationColor(builder, *diffuse[:3]))
    PresentationMaterial.PresentationMaterialAddEmissionScale(
        builder, PresentationColor.CreatePresentationColor(builder, *emission[:3]))
    PresentationMaterial.PresentationMaterialAddRoughness(builder, params["roughness"])
    PresentationMaterial.PresentationMaterialAddMetallic(builder, params["metallic"])
    PresentationMaterial.PresentationMaterialAddSpecular(builder, params["specular"])
    PresentationMaterial.PresentationMaterialAddWrapS(builder, PresentationWrap.PresentationWrap.Repeat)
    PresentationMaterial.PresentationMaterialAddWrapT(builder, PresentationWrap.PresentationWrap.Clamp)
    material = PresentationMaterial.PresentationMaterialEnd(builder)
    path_offset = builder.CreateString(mesh_path)
    points_offset = builder.CreateString(points_path)
    offsets_vector = builder.CreateNumpyVector(np.asarray(offsets, dtype=np.uint32))
    narrow = point_count <= 65535
    indices_vector = builder.CreateNumpyVector(
        np.asarray(indices, dtype=np.uint16 if narrow else np.uint32))
    weights_vector = builder.CreateNumpyVector(np.asarray(weights, dtype=np.float32))
    vertices_vector = builder.CreateNumpyVector(np.asarray(render_vertices, dtype=np.uint32))
    uvs_vector = builder.CreateNumpyVector(np.asarray(render_uvs, dtype=np.float32))
    PresentationMesh.PresentationMeshStart(builder)
    PresentationMesh.PresentationMeshAddPath(builder, path_offset)
    PresentationMesh.PresentationMeshAddPointsPath(builder, points_offset)
    PresentationMesh.PresentationMeshAddSourcePointCount(builder, point_count)
    PresentationMesh.PresentationMeshAddStencilOffsets(builder, offsets_vector)
    if narrow:
        PresentationMesh.PresentationMeshAddStencilIndices16(builder, indices_vector)
    else:
        PresentationMesh.PresentationMeshAddStencilIndices32(builder, indices_vector)
    PresentationMesh.PresentationMeshAddStencilWeights(builder, weights_vector)
    PresentationMesh.PresentationMeshAddVertexIndices(builder, vertices_vector)
    PresentationMesh.PresentationMeshAddUvs(builder, uvs_vector)
    PresentationMesh.PresentationMeshAddMaterial(builder, material)
    mesh = PresentationMesh.PresentationMeshEnd(builder)
    meshes_vector = table_vector(builder, Presentation.PresentationStartMeshesVector, [mesh])
    source_json = builder.CreateString(
        json.dumps(manifest, separators=(",", ":"), allow_nan=False))
    Presentation.PresentationStart(builder)
    Presentation.PresentationAddVersion(builder, 1)
    Presentation.PresentationAddControls(builder, controls_vector)
    Presentation.PresentationAddMeshes(builder, meshes_vector)
    Presentation.PresentationAddSourceJson(builder, source_json)
    root = Presentation.PresentationEnd(builder)
    builder.Finish(root, file_identifier=b"REXP")
    data = bytes(builder.Output())
    # Read back what a loader reads.
    assert Presentation.Presentation.PresentationBufferHasIdentifier(data, 0)
    check = Presentation.Presentation.GetRootAs(data, 0)
    assert check.Version() == 1 and check.ControlsLength() == len(controls)
    assert check.MeshesLength() == 1
    stored = check.Meshes(0)
    assert stored.StencilOffsetsLength() == 3 * len(stencils) + 1
    assert stored.StencilWeightsLength() == len(weights)
    assert (stored.StencilIndices16Length() if narrow else stored.StencilIndices32Length()) == len(indices)
    assert stored.VertexIndicesLength() == len(render_vertices)
    assert stored.UvsLength() == len(render_uvs)
    assert stored.Material().TexturePngLength() == len(png)
    return data


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
    params = {name: surface.GetInput(name).Get() for name in ("metallic", "roughness", "specular")}
    diffuse = inputs["diffuseColor"]["scale"]
    emission = inputs["emissiveColor"]["scale"]
    manifest = dict(source="usdRig/docs/examples/tutorial_rolling_ball_free.usda", prim=str(mesh.GetPath()),
                    material=str(material.GetPath()), points=len(points), faces=len(counts),
                    corners=len(indices), uv_interpolation=str(st.GetInterpolation()),
                    outward_winding_corrected_faces=reversed_faces,
                    fvar_rule=rule, subdivision="OpenSubdiv Catmull-Clark, level 2, limit surface",
                    texture_sha256=hashlib.sha256(texture.read_bytes()).hexdigest(),
                    diffuse_scale=diffuse, emission_scale=emission, **params)
    (assets / "source_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # The OBJ and stencil files above are bake intermediates; Godot loads
    # only the presentation embedded in the .rigexec.
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
            controls.append((public + "." + channel, str(attr.GetPath()), UNITS[channel[0]]))
    assert controls and len({name for name, _, _ in controls}) == len(controls)
    stencils = json.loads((assets / "tutorial_ball.stencils.json").read_text())
    assert len(stencils) == len(vertices)
    data = build_presentation(
        controls, str(mesh.GetPath()), str(mesh.GetPointsAttr().GetPath()), len(points),
        stencils, render_vertices, render_uvs, texture.read_bytes(), diffuse, emission,
        params, manifest)
    target = assets / "presentation.rexp"
    target.write_bytes(data)
    print(f"Wrote mesh, subdivision stencils, material, PNG and {len(controls)} public controls "
          f"to {target.name} ({len(data):,} bytes); rigExecBake --presentation embeds it")
    print(f"Exported {len(points)} source points, {len(counts)} faces, original st UVs and {material.GetPath()}")


if __name__ == "__main__":
    main()
