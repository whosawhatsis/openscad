#pragma once

class GLView;

// Draw the common geometric feature mask, optionally augmented by raw color steps.
void drawFeatureEdges(GLView& view, bool colorEdges, bool overlay);
