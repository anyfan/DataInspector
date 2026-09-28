#pragma once

#include <QColor>
#include <QSGMaterial>

// Flat-colour material with an optional order-independent blend mode so that
// curves drawn on top of each other remain distinguishable.
//
// - Opaque: identical to QSGFlatColorMaterial (last curve wins).
// - Darken: framebuffer = min(src, dst). On a light background a lone curve
//   keeps its colour, and the overlap of two curves shows a darker mixture.
// - Lighten: framebuffer = max(src, dst); the dark-theme counterpart.
//
// min/max are idempotent, so the overlapping joint quads of one series never
// change its own colour, unlike alpha or multiply blending.
class PlotBlendMaterial final : public QSGMaterial
{
public:
    enum Mode { Opaque = 0, Darken = 1, Lighten = 2 };

    explicit PlotBlendMaterial(Mode mode = Opaque, const QColor &color = Qt::black);

    QSGMaterialType *type() const override;
    int compare(const QSGMaterial *other) const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode renderMode) const override;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    const QColor &color() const { return m_color; }
    void setColor(const QColor &color);

    static Mode clampMode(int mode);

private:
    Mode m_mode = Opaque;
    QColor m_color;
};
