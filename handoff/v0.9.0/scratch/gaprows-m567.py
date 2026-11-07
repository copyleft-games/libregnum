import sys
path='docs/editor/gap-analysis.org'
W='(M6/M7 editor wave)'
sec={
 213:("have (0.9.0, engine): 2D shadow geometry from tile collision, =LrgShadowOccluder2D= and sprite outlines, soft shadows in the lighting pass","none","occluder authoring "+W),
 236:("have (0.9.0, engine): =.rparticles= module stacks (20 module kinds, bursts, sub-emitters, collision) with seeded deterministic playback; =LrgParticleComponent=","none","particle editor with live preview "+W),
 237:("have (0.9.0, engine): =LrgGpuParticles= transform-feedback emitter (100k+) with a bit-identical CPU twin, running the GPU subset of =.rparticles=","none","GPU toggle in the particle editor "+W),
 238:("partial (0.9.0): Niagara-style module stacks per emitter in =.rparticles= (stacks, not node graphs)","none","module-stack editor "+W),
 239:("have (0.9.0, engine): =LrgParticleRibbonRenderer= (trails, connected ribbons, UV modes) and =LrgParticleMeshRenderer= (instanced)","none","preview in the particle editor "+W),
 240:("have (engine, 0.8.1 preview; 0.9.0 =.rweather= presets, layers and transitions)","none","weather inspector "+W),
 248:("have (0.9.0): =LrgPhysicsWorld= with debug draw; 2D collider gizmos for the =collider= component in 2D levels","none","none"),
 249:("have (0.9.0): =LrgDynamicsWorld= rewritten after Jolt (GJK/EPA, box/convex/mesh/heightfield shapes, BVH broadphase, persistent manifolds, CCD, islands and sleeping, collision layers); =rigid-body3d=/=collider3d= components; physics panel and collider gizmos","none","none"),
 250:("have (0.9.0): fixed, hinge, slider, distance, cone and 6-DOF joints with limits, springs, motors and break forces (=LrgJoint=, =joint3d=); joint gizmos","none","none"),
 251:("have (0.9.0): node =physics:= block (body, shapes, joints) in =.rlevel= and prefabs with overrides; project =physics:= layers and settings","none","none"),
 252:("have (0.9.0): shapes, contacts, AABBs, joints, sleeping and centre of mass for both worlds; viewport Physics overlay","none","none"),
 253:("have (0.9.0): =.rphysasset= ragdolls (=LrgRagdoll=), PBD cloth (=LrgCloth=, =cloth3d=), Voronoi pre-fracture (=LrgFracture=, =destructible3d=); ragdoll editor","none","cloth and fracture authoring beyond the inspector"),
 262:("have (0.9.0, engine): =.rnav= bake settings (=LrgNavBake=) baking from =LrgWorld3D= geometry with filters, areas with costs, off-mesh links; level =navigation:= block","none","bake panel and navmesh overlay "+W),
 263:("have (0.9.0, engine): =LrgNavAgent= / =LrgNavCrowd= (corridor following, steering, ORCA against agents and walls) and =LrgNavAgentComponent=","none","agent debug view "+W),
 264:("have (0.9.0, engine): =.rbt= trees with C and script leaf bindings, blackboard schema, subtrees, live debug state; =LrgAiComponent=","none","BT editor with live state "+W),
 265:("have (0.9.0, engine): =.rfsm= hierarchical logic state machines (events, timers, conditions, actions); =LrgAiComponent=","none","state-machine editor "+W),
 266:("engine-only: =LrgDecisionBrain=, =LrgDecider=; 0.9.0 inspector hooks (topics, requests, latency, totals)","none","decisions inspector "+W),
 274:("have (0.9.0, engine): =.rterrain= (chunks, height/splat/hole sidecars, LOD), =LrgTerrainBrush= edits with undo records, heightfield physics, =terrain= node kind","none","sculpt/paint brushes "+W),
 275:("have (0.9.0, engine): =LrgFoliage= scatter layers (mesh, billboard, grass) with density maps, slope/height rules and LOD fade","none","foliage brushes "+W),
 276:("have (0.9.0, engine): =.rwater= planes, lakes and rivers (=LrgWaterBody=, =LrgWaterComponent=, =water= node kind)","none","water inspector "+W),
 285:("have (0.9.0, engine): =.rmixer= buses, sends, effect chains, snapshots with blends, ducking, peak/RMS meters; project default mixer","none","mixer panel with meters "+W),
 287:("have (0.9.0, engine): spatial audio-source/listener components with attenuation curves, min/max distance, cones, doppler, occlusion and bus routing","none","attenuation gizmo "+W),
 310:("have (0.9.0, engine): =LrgRuleTile= 3x3 neighbour masks, 47- and 16-tile presets, weighted random variants","none","rule-tile editor "+W),
 311:("have (0.9.0, engine): per-tile flip/rotation bits; collision polygons, custom data and animation frames in =.rtileset=","none","tile properties editor "+W),
 312:("have (0.9.0, engine): isometric, staggered and hexagonal grids with coordinate conversion, picking and draw order; node grid keys in =.rlevel=","none","iso/hex painting "+W),
 314:("have (0.9.0, engine): =.rsprite= slicing (grid or rects, pivots), named frames, frame animations with events","none","sprite slicer and frame animation editor "+W),
 316:("have (0.9.0, engine): see Lighting (2D shadow geometry drawn, soft shadows)","none","none"),
}
last=[461,463,466,467,468,469,485,486,487,488,491]
lines=open(path).read().split('\n')
for n,(st,eg,ed) in sec.items():
    c=lines[n-1].split('|'); assert len(c)==11,(n,len(c),lines[n-1][:60])
    c[5]=' '+st+' '; c[6]=' '+eg+' '; c[7]=' '+ed+' '; lines[n-1]='|'.join(c)
for n in last:
    c=lines[n-1].split('|'); assert c[-1]=='' and len(c)>=7,(n,len(c))
    c[-2]=' have (0.9.0) '; lines[n-1]='|'.join(c)
open(path,'w').write('\n'.join(lines))
