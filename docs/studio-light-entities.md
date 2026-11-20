# Studio light entities

Scene lights use the common editor document entity adapter. They expose a
persisted identity and two component descriptors: `Transform` and `Light`.
Hierarchy, native viewport picking, Inspector and Lighting share selection by
`light:<entityId>`; array positions are not the identity.

The entity owns its name. Transform owns X/Z and vertical position; RTS vertical
position retains the legacy height-above-terrain convention, FPS uses world Y.
Light owns type, radius, intensity, color and lighting flags. Native Inspector
snapshots publish `entityProperties` and `components[{id,title,properties}]`.
`studio.setProperty.componentId` identifies the owner; mismatched ownership is
rejected before any mutation. Transform writes use the common transform adapter;
Light writes use `writeDocumentComponent`. Both commit through native history
and FPS full-document validation where applicable.

For existing maps, `lights[]` remains the single canonical serialized payload,
with additive `entityId` metadata. There is no independently mutable entity-light
mirror. Loading old maps assigns deterministic legacy identities; saving persists
them. New sources receive unique identities, duplicate authored IDs are repaired.
Deleting/reordering other lights does not change an existing ID. New Lighting UI
update/delete requests send the entity ID; the native side resolves the current
ordinal and rejects deleted identities. Old ordinal-based APIs remain compatible.

Presentation-only light pick handles do not enter GI caster/receiver geometry.
RTS handles include terrain height so they coincide with the rendered source.
This adapter does not introduce GPU handles or float presentation state into
canonical deterministic gameplay ECS. It does not claim migration of every other
legacy document object or conversion of legacy ImGui lighting UI. Generic dynamic
component attach/remove and the broader gameplay ECS migration remain separate
features. New scene objects must follow the universal entity rule in AGENTS.md.
