# Native rocks gallery

Implementation 53f8c25a00e9983ec23b39f158855b3170d32298 on gh-pages. Added one card, two actual Metal scene-only captures, and native API link; total eight cards. Generator engine commit f22e60ae6, published with handoff 20e7c447d to feature/terraweave. Scope text distinguishes authored scene anchors from native scatter and explicitly excludes automatic streaming, physics and Studio panel. No engine main merge.

Validation executed: Python HTMLParser local asset references all resolve, eight articles assertion PASS, git diff --check PASS. Source screenshots visually inspected at 1600x1000. Responsive browser regression NOT RUN (existing card markup/styles unchanged). No CI/deployment verification; source push is not proof of Pages deployment.

Next: verify Pages deployment and responsive card layout when browser available.
