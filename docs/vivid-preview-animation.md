# VIVID default preview idle and poses

Studio → VIVID Appearance → Idle лица и позы. Binding a character defaults to
playing, speed1, facial idle enabled, relaxed stance. Controls provide pause,
0.25–2× speed, facial idle toggle and relaxed / A-pose / T-pose / portrait.
These settings are native session state, not project asset edits or export data.

`HumanPreviewAnimation.h` builds eight six-second clips (four poses × face on/off)
on the unpublished preview model. Existing source clips remain intact. One
existing Studio Animator samples the selected generated clip. Changing a pose
crossfades over0.25s; while paused it switches immediately. Manual FacialControlRig
is applied afterward, retaining precedence. No second scheduler or authoritative
ECS state is introduced. Creator uses its existing MotionWeave AnimationGraph.

Body pose solves each named upperarm toward its desired world direction using
its descendant lowerarm, converting world rotation through the bind parent frame.
Numeric importer suffixes and wardrobe namespaces are accepted. All corresponding
body/clothing rigs are processed. Named neck_01 and spine_03 receive small periodic
rotations. The portrait pose adds a small head turn. This is a preview pose helper,
not full anatomical retarget/IK or MetaHuman body correctives; incompatible names
are skipped. Foot planting and locomotion are not implemented.

Face channels are resolved from each mesh's own targetNames; native current
kMaxMorphTargets capacity is64, not the obsolete8 mentioned in older docs.
Generated samples include synchronized blinks at1.7/4.4s, subtle mouthSmileLeft/
Right and browInnerUp. Unrelated morphs retain source defaults. No fabricated
facial mesh or replacement proprietary RigLogic is generated. Source morphs,
materials and topology determine the visual quality of eyelids/eyelashes.

`studio.humanPreviewAnimation` gets settings or validates/replaces
`{pose,playing,faceIdle,speed}`. Changes do not rebuild GPU meshes or change the
character binding generation. Preview clips are not baked by Assemble character.

Creator: `--preview-pose relaxed|a_pose|t_pose|portrait`. Its native preview
morph path bypasses the sparse actor vertex override (which deliberately suppresses
Model morphs) and uses ModelDraw.pose, matching Studio. Legacy Creator modes retain
their actor path. `--preview-time 0..6` samples an explicit diagnostic copy at a
fixed time without mutating MotionWeave's canonical evaluated pose. Combine with
--smoke --still --portrait and --studio-lighting catalog for repeatable snapshots.
A fixed-time snapshot is not evidence of real-time animation pacing.

Also corrected studio cyclorama/light placement to use the same normalized
character coordinates as StudioFacialGpuPreview (extent→2); raw asset-world bounds
previously disagreed with this preview transform. Source/export geometry is untouched.

Visual checkpoint: default relaxed/A-pose and open/closed portrait were rendered
on Ada. T-pose exposes shoulder/skin poke-through in its shirt; this requires
asset body masks or corrective deformation and is NOT clean wardrobe acceptance.
Known source eyelash alpha loss and hair-card artifacts remain visible.
