#ifndef PLOTDISPLAY_H
#define PLOTDISPLAY_H

#include <map>
#include <qwt/qwt_plot.h>
#include <qwt/qwt_plot_curve.h>

using std::map;

class PlotDisplay : public QwtPlot
{
    Q_OBJECT

public:
    static vector<QString> curveNames;
    map<QString, QwtPlotCurve*> curveMap;
    map<QString, bool> curvesEnabled;

public:
    explicit PlotDisplay(QWidget *parent = 0);
    virtual ~PlotDisplay();
    
signals:
    
public slots:
    void clear();
    void updateCurve(const QwtPlotCurve *curve, QString name);
    virtual void replot();
};

#endif // PLOTDISPLAY_H
