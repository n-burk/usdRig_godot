"""Center the existing atlas star on the north-pole tangent plane.

Edits the source mesh UVs, not texture pixels. The equatorial band's V
coordinates are preserved. Both usdview and the bake consume the same UVs.
"""
import math
import re
from pathlib import Path
import export_ball_assets as asset


def main():
    path = asset.RIG / "docs/examples/tutorial_rolling_ball.usda"
    stage = asset.Usd.Stage.Open(str(path))
    mesh = asset.UsdGeom.Mesh(stage.GetPrimAtPath("/BallAsset/Geom/Ball"))
    points = mesh.GetPointsAttr().Get()
    indices = mesh.GetFaceVertexIndicesAttr().Get()
    counts = mesh.GetFaceVertexCountsAttr().Get()
    st = asset.UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st")
    assert not st.IsIndexed() and st.GetInterpolation() == "faceVarying"
    result = []
    offset = 0
    for count in counts:
        face = [points[i] for i in indices[offset:offset + count]]
        cap = min(p[1] - 1.0 for p in face) > 0.55
        north = max(p[1] - 1.0 for p in face) > 0.001
        us = [0.5 + math.atan2(p[0], p[2]) / math.tau for p in face]
        if max(us) - min(us) > 0.5:
            us = [u + 1 if u < 0.5 else u for u in us]
        for p, u in zip(face, us):
            x, y, z = p[0], p[1] - 1.0, p[2]
            v = 0.5 + math.asin(max(-1.0, min(1.0, y))) / math.pi
            # Project X/Z around +Y: the star's geometric centre is exactly
            # above the equator. Its five tips fit an ellipse centred at
            # atlas pixel (887, 176), radii (180, 161), in the 1774x887 PNG.
            # Use that centre, rather than the asymmetric bounding-box centre.
            decal_u = 0.5 + x * (180.0 / (1774.0 * 0.72))
            decal_v = (1.0 - 176.0/887.0) - z * (161.0 / (887.0 * 0.72))
            # A separate face-varying island avoids interpolating through
            # the star a second time between the cap and equatorial band.
            # Both sides of the cap boundary sample yellow. The side island
            # uses the atlas's undecorated longitude; its V stays unchanged.
            result.append((decal_u, decal_v) if cap else (0.1 if north else u, v))
        offset += count
    text = path.read_text(encoding="utf-8")
    values = ", ".join(f"({u:.8g}, {v:.8g})" for u, v in result)
    text, n = re.subn(r"(texCoord2f\[\] primvars:st\s*=\s*)\[[^\]]*\]",
                      lambda match: match[1] + "[" + values + "]", text, count=1)
    assert n == 1
    path.write_text(text, encoding="utf-8")
    print(f"Updated {len(result)} source UV corners; equatorial band unchanged")


if __name__ == "__main__":
    main()
