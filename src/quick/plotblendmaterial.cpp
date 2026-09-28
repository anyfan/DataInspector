#include "plotblendmaterial.h"

#include <QSGMaterialShader>
#include <QVector4D>

#include <cstring>

namespace {

// Reuses the scene-graph flat-colour shaders (mat4 matrix per view followed by
// one vec4 colour) and only changes the blend state of the pipeline.
class PlotBlendShader final : public QSGMaterialShader
{
public:
    PlotBlendShader(PlotBlendMaterial::Mode mode, int viewCount)
        : m_mode(mode)
    {
        setShaderFileName(VertexStage,
                          QStringLiteral(":/qt-project.org/scenegraph/shaders_ng/flatcolor.vert.qsb"),
                          viewCount);
        setShaderFileName(FragmentStage,
                          QStringLiteral(":/qt-project.org/scenegraph/shaders_ng/flatcolor.frag.qsb"),
                          viewCount);
        if (m_mode != PlotBlendMaterial::Opaque)
            setFlag(UpdatesGraphicsPipelineState, true);
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial,
                           QSGMaterial *oldMaterial) override
    {
        QByteArray *buffer = state.uniformData();
        const int viewCount = newMaterial->viewCount();
        const int matrixBytes = 64 * viewCount;
        if (buffer->size() < matrixBytes + 16)
            return false;
        bool changed = false;
        if (state.isMatrixDirty()) {
            for (int view = 0; view < viewCount; ++view) {
                const QMatrix4x4 matrix = state.combinedMatrix(view);
                std::memcpy(buffer->data() + 64 * view, matrix.constData(), 64);
            }
            changed = true;
        }
        auto *material = static_cast<PlotBlendMaterial *>(newMaterial);
        auto *previous = static_cast<PlotBlendMaterial *>(oldMaterial);
        if (!previous || material->color() != previous->color() || state.isOpacityDirty()) {
            const QColor color = material->color();
            const float opacity = state.opacity() * float(color.alphaF());
            const QVector4D premultiplied(float(color.redF()) * opacity,
                                          float(color.greenF()) * opacity,
                                          float(color.blueF()) * opacity,
                                          opacity);
            std::memcpy(buffer->data() + matrixBytes, &premultiplied, 16);
            changed = true;
        }
        return changed;
    }

    bool updateGraphicsPipelineState(RenderState &, GraphicsPipelineState *pipeline,
                                     QSGMaterial *, QSGMaterial *) override
    {
        if (m_mode == PlotBlendMaterial::Opaque)
            return false;
        pipeline->blendEnable = true;
        pipeline->separateBlendFactors = false;
        pipeline->srcColor = GraphicsPipelineState::One;
        pipeline->dstColor = GraphicsPipelineState::One;
        pipeline->srcAlpha = GraphicsPipelineState::One;
        pipeline->dstAlpha = GraphicsPipelineState::One;
        pipeline->opColor = m_mode == PlotBlendMaterial::Darken
            ? GraphicsPipelineState::BlendOp::Min
            : GraphicsPipelineState::BlendOp::Max;
        // Keep the destination coverage: never let a blended curve punch a
        // hole into the alpha channel of a translucent window.
        pipeline->opAlpha = GraphicsPipelineState::BlendOp::Max;
        return true;
    }

private:
    PlotBlendMaterial::Mode m_mode;
};

} // namespace

PlotBlendMaterial::PlotBlendMaterial(Mode mode, const QColor &color)
    : m_color(color)
{
    setMode(mode);
}

PlotBlendMaterial::Mode PlotBlendMaterial::clampMode(int mode)
{
    switch (mode) {
    case Darken: return Darken;
    case Lighten: return Lighten;
    default: return Opaque;
    }
}

QSGMaterialType *PlotBlendMaterial::type() const
{
    static QSGMaterialType types[3];
    return &types[int(m_mode)];
}

int PlotBlendMaterial::compare(const QSGMaterial *other) const
{
    const auto *material = static_cast<const PlotBlendMaterial *>(other);
    if (m_mode != material->m_mode)
        return int(m_mode) - int(material->m_mode);
    const QRgb mine = m_color.rgba();
    const QRgb theirs = material->m_color.rgba();
    return mine == theirs ? 0 : (mine < theirs ? -1 : 1);
}

QSGMaterialShader *PlotBlendMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new PlotBlendShader(m_mode, viewCount());
}

void PlotBlendMaterial::setMode(Mode mode)
{
    m_mode = mode;
    // Blended nodes must stay in the (order-preserving, depth-test free) alpha
    // pass; otherwise the opaque pass' depth test would reject the overlap.
    setFlag(Blending, m_mode != Opaque || m_color.alpha() != 255);
}

void PlotBlendMaterial::setColor(const QColor &color)
{
    m_color = color;
    setFlag(Blending, m_mode != Opaque || m_color.alpha() != 255);
}
