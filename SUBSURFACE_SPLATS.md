# Subsurface splats

Replaces the random walk for an object with a subsurface material by a trained cloud of gaussians, sampled in a
few lookups per subsurface event.

## Using it

1. Give the object a material with the **subsurface** lighting model (scattering, absorbtion, transport scale,
   anisotropy, subsurface ratio as before).
2. Select the object → *Edit Object* → **Subsurface Splats** → *Train Subsurface Splats*. Training runs on the CPU
   in time slices (the editor stays usable); progress shows under the button and the accuracy report goes to the
   console when it finishes.
3. The cloud is linked to that object and used by the path tracer automatically. Camera settings:
   - **Subsurface Splats**: off = always use the random walk (A/B comparison).
   - **SSS Exit Lighting**: next event estimation at the exit point (the random walk has no equivalent, see below).
4. Clouds are saved next to the session as `session.prism.sss` and restored with it.

A cloud stays valid while the object's triangle range is unchanged. The panel flags it as out of date if the
scattering density (`luminance(sigma_s + sigma_a) * scale`), the anisotropy, or the object scale changes. Absorption
and scattering *colour* can change freely, because throughput is computed from the sampled path length at render
time. *Preview Internal Cloud* adds the internal gaussians to the scene as visible splats, coloured blue (short paths)
to red (long paths).

## What is learnt (src/renderer/sssfit.c)

Everything is fitted in object space to geometric random walks (free flight density `sigma`, HG phase `g`) traced
against the object's own triangles. 10% of the walks are held out for validation.

| piece | representation | used for |
|---|---|---|
| surface gaussians | flattened 3D gaussians on the surface; per triangle table of 3 candidates (O(1) query, stochastic blend by footprint weight) | the "UV lookup" (triangle id + position, since meshes have no UVs) |
| exit lobes | per surface gaussian, a mixture of up to 8 3D gaussians over `exit - entry` (EM), each with a projection axis | sample a point, project it onto the mesh along the axis (1–2 ray casts, redrawn on a miss) |
| exit direction | per surface gaussian, cosine power lobe (axis + exponent from the mean cosine) folded onto the outward hemisphere | sample the continuation direction; its density is the BSDF for exit NEE |
| internal cloud | k-means of walk scattering locations → oriented 3D gaussians with a small BVH | ray traced along the entry→exit chord: line integrals blend 7 coefficients per gaussian into a 2-component log-normal over `log(excess length + 0.1 mfp)` (Adam on the NLL) |
| event model | `E[a^n given L] ≈ a^(1+c) exp(-k sigma (1-a) L)`, `k`, `c` fitted per object | throughput from path length only: `exp(-sigma_t L) a^(1+c) exp(-k sigma (1-a) L)`, `a = sigma_s / sigma`, which is exactly the estimator `subsurface_walk` uses |

`sssfit_sample()` is a line-by-line C mirror of `sss_splat_scatter()` in `shaders/path.comp`. The validation report
uses it, so the numbers describe what the GPU does. Keep the two in sync.

`tests/sssfit_harness.c` runs the whole fit on synthetic meshes without the renderer:

```
gcc -O2 -DSSSFIT_STANDALONE -Isrc tests/sssfit_harness.c src/renderer/sssfit.c -lm -o sssfit_harness && ./sssfit_harness
```

## Changes to the random walk reference

These are small fixes so that the walk can serve as ground truth:

- the phase function inside the medium was sampled around `-ray.direction`, which turned forward scattering (g > 0)
  into back scattering; it is now sampled around `ray.direction`
- the first direction was HG around the *outgoing* (camera facing) direction, so many walks started outside the
  object; it now follows the incident direction and is mirrored into the medium if it points out
- a walk whose boundary trace misses (cracks / started outside) now terminates with zero throughput instead of
  "exiting" at `position - direction`

Still as before, and worth knowing when you compare: walks cut by *Max SSS Bounces* (default 32) or by Russian
roulette continue from inside the object, and the walk has no light sampling at the exit. For a fair A/B, raise
Max SSS Bounces, and compare with *SSS Exit Lighting* off, or light the scene with emissive geometry and direct
lighting off.

## Limits

- Rigid transforms (and uniform scale at the trained scale) are exact; skinned / animated deformation is not
  modelled (bind pose cloud).
- Closed, consistently "solid" meshes. Outward normals are found by ray parity, so winding does not matter. The fit
  fails if more than half the walks leak out.
- Very low albedo channels are approximated less well. The report shows E[a^n] walk vs model for a = 0.5 / 0.9 / 0.99.
