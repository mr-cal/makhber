/***************************************************************************
    File                 : MultiLayer.cpp
    Project              : Makhber
    --------------------------------------------------------------------
    Copyright            : (C) 2006 by Ion Vasilief, Tilman Benkert
    Email (use @ for *)  : ion_vasilief*yahoo.fr, thzs*gmx.net
    Description          : Multi layer widget

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *  This program is free software; you can redistribute it and/or modify   *
 *  it under the terms of the GNU General Public License as published by   *
 *  the Free Software Foundation; either version 2 of the License, or      *
 *  (at your option) any later version.                                    *
 *                                                                         *
 *  This program is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *  GNU General Public License for more details.                           *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor,                    *
 *   Boston, MA  02110-1301  USA                                           *
 *                                                                         *
 ***************************************************************************/

#include "MultiLayer.h"

#include "plot2D/Plot.h"
#include "plot2D/Legend.h"
#include "plot2D/SelectionMoveResizer.h"

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_layout.h>
#include <qwt_scale_widget.h>
#include <qwt_text_label.h>

#include <gsl/gsl_vector.h>

#include <QVector>
#include <QWidgetList>
#include <QPrinter>
#include <QPrintDialog>
#include <QDateTime>
#include <QApplication>
#include <QMessageBox>
#include <QBitmap>
#include <QImageWriter>
#include <QPainter>
#include <QPicture>
#include <QClipboard>
#include <QMouseEvent>
#include <QSvgGenerator>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

LayerButton::LayerButton(const QString &text, QWidget *parent) : QPushButton(text, parent)
{
    int btn_size = 20;

    setCheckable(true);
    setChecked(true);
    setMaximumWidth(btn_size);
    setMaximumHeight(btn_size);
}

void LayerButton::mousePressEvent(QMouseEvent *event)
{
    if (!isChecked())
        Q_EMIT layerButtonclicked(this);
    if (event->button() == Qt::RightButton)
        Q_EMIT showContextMenu();
}

void LayerButton::mouseDoubleClickEvent(QMouseEvent *)
{
    Q_EMIT showCurvesDialog();
}

MultiLayer::MultiLayer(const QString &label, QWidget *parent, const QString &name,
                       Qt::WindowFlags f)
    : MyWidget(label, parent, name, f)
{
    if (name.isEmpty())
        setObjectName("multilayer plot");

    QDateTime dt = QDateTime::currentDateTime();
    setBirthDate(QLocale::c().toString(dt, "dd-MM-yyyy hh:mm:ss:zzz"));

    graphs = 0;
    cols = 1;
    rows = 1;
    graph_width = 500;
    graph_height = 400;
    colsSpace = 5;
    rowsSpace = 5;
    left_margin = 5;
    right_margin = 5;
    top_margin = 5;
    bottom_margin = 5;
    l_canvas_width = 400;
    l_canvas_height = 300;
    lastSize = QSize(-1, -1);
    hor_align = HCenter;
    vert_align = VCenter;
    active_graph = nullptr;
    addTextOn = false;
    d_scale_on_print = true;
    d_print_cropmarks = false;

    layerButtonsBox = new QHBoxLayout();
    auto *hbox = new QHBoxLayout();
    // add a zero width widget to reserve space for the layer's buttons
    auto *strut = new QWidget();
    strut->setFixedSize(0, LayerButton::btnSize());
    hbox->addWidget(strut);
    hbox->addLayout(layerButtonsBox);
    hbox->addStretch();

    canvas = new QWidget();
    canvas->installEventFilter(this);

    auto *d_main_widget = new QWidget();
    auto *layout = new QVBoxLayout();
    d_main_widget->setLayout(layout);
    layout->addLayout(hbox);
    layout->addWidget(canvas, 1);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    this->setWidget(d_main_widget);

    d_main_widget->setAutoFillBackground(true);
    d_main_widget->setBackgroundRole(QPalette::Window);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(Qt::white));
    d_main_widget->setPalette(pal);

    setMinimumSize(200, 150);
    setGeometry(QRect(0, 0, graph_width, graph_height));
    setFocusPolicy(Qt::StrongFocus);
}

Graph *MultiLayer::layer(int num)
{
    return dynamic_cast<Graph *>(graphsList.at(num - 1));
}

LayerButton *MultiLayer::addLayerButton()
{
    for (int i = 0; i < buttonsList.count(); i++) {
        auto *btn = dynamic_cast<LayerButton *>(buttonsList.at(i));
        btn->setChecked(false);
    }

    auto *button = new LayerButton(QString::number(++graphs));
    connect(button, SIGNAL(layerButtonclicked(LayerButton *)), this,
            SLOT(activateGraph(LayerButton *)));
    connect(button, SIGNAL(showContextMenu()), this, SIGNAL(showLayerButtonContextMenu()));
    connect(button, SIGNAL(showCurvesDialog()), this, SIGNAL(showCurvesDialog()));

    buttonsList.append(button);
    layerButtonsBox->addWidget(button);
    return button;
}

Graph *MultiLayer::addLayer(int x, int y, int width, int height)
{
    addLayerButton();
    if (!width && !height) {
        width = graph_width;
        height = graph_height;
    }

    auto *g = new Graph(canvas);
    g->setAttribute(Qt::WA_DeleteOnClose);
    g->setGeometry(x, y, width, height);
    g->plotWidget()->resize(QSize(width, height));
    graphsList.append(g);

    active_graph = g;
    g->show();
    connectLayer(g);
    return g;
}

void MultiLayer::activateGraph(LayerButton *button)
{
    for (int i = 0; i < buttonsList.count(); i++) {
        auto *btn = dynamic_cast<LayerButton *>(buttonsList.at(i));
        if (btn->isChecked())
            btn->setChecked(false);

        if (btn == button) {
            active_graph = dynamic_cast<Graph *>(graphsList.at(i));
            active_graph->setFocus();
            active_graph->raise(); // raise layer on top of the layers stack
            button->setChecked(true);
        }
    }
}

void MultiLayer::setActiveGraph(Graph *g)
{
    if (!g || active_graph == g)
        return;

    active_graph = g;
    active_graph->setFocus();

    if (d_layers_selector)
        delete d_layers_selector;

    active_graph->raise(); // raise layer on top of the layers stack

    for (int i = 0; i < (int)graphsList.count(); i++) {
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        auto *btn = dynamic_cast<LayerButton *>(buttonsList.at(i));
        if (gr == g)
            btn->setChecked(true);
        else
            btn->setChecked(false);
    }
}

void MultiLayer::contextMenuEvent(QContextMenuEvent *e)
{
    Q_EMIT showWindowContextMenu();
    e->accept();
}

void MultiLayer::resizeLayers(const QResizeEvent *re)
{
    QSize oldSize = re->oldSize();
    QSize size = re->size();

    if (!oldSize.isValid()) {
        if (lastSize.isValid() && isVisible()) {
            oldSize = lastSize;
        } else { // TODO: for maximized windows, the size of the layers should be saved in % of the
                 // workspace area in order to restore
            // the layers properly! For the moment just resize the layers to occupy the whole
            // canvas, although the restored geometry might be altered!
            oldSize = QSize(canvas->childrenRect().width() + left_margin + right_margin,
                            canvas->childrenRect().height() + top_margin + bottom_margin);
        }
    }

    resizeLayers(size, oldSize, false);
}

void MultiLayer::resizeLayers(const QSize &size, const QSize &oldSize, bool scaleFonts)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);

    double w_ratio = (double)size.width() / (double)oldSize.width();
    double h_ratio = (double)(size.height()) / (double)(oldSize.height());

    for (int i = 0; i < graphsList.count(); i++) {
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        if (gr && !gr->ignoresResizeEvents()) {
            int gx = qRound(gr->x() * w_ratio);
            int gy = qRound(gr->y() * h_ratio);
            int gw = qRound(gr->width() * w_ratio);
            int gh = qRound(gr->height() * h_ratio);

            gr->setGeometry(QRect(gx, gy, gw, gh));
            gr->plotWidget()->resize(QSize(gw, gh));

            if (scaleFonts)
                gr->scaleFonts(h_ratio);
        }
    }

    lastSize = size;
    Q_EMIT modifiedPlot();
    Q_EMIT resizedWindow(this);
    QApplication::restoreOverrideCursor();
}

void MultiLayer::confirmRemoveLayer()
{
    if (graphs > 1) {
        switch (QMessageBox::information(
                this, tr("Guess best layout?"),
                tr("Do you want Makhber to rearrange the remaining layers?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)) {
        case QMessageBox::Yes:
            removeLayer();
            arrangeLayers(true, false);
            break;

        case QMessageBox::No:
            removeLayer();
            break;

        default:
            return;
            break;
        }
    } else {
        removeLayer();
    }
}

void MultiLayer::removeLayer()
{
    // remove corresponding button
    LayerButton *btn = nullptr;
    int i = 0;
    for (i = 0; i < buttonsList.count(); i++) {
        btn = dynamic_cast<LayerButton *>(buttonsList.at(i));
        if (btn->isChecked()) {
            buttonsList.removeAll(btn);
            btn->close();
            break;
        }
    }

    // update the texts of the buttons
    for (i = 0; i < buttonsList.count(); i++) {
        btn = dynamic_cast<LayerButton *>(buttonsList.at(i));
        btn->setText(QString::number(i + 1));
    }

    if (active_graph->zoomOn() || active_graph->activeTool())
        Q_EMIT setPointerCursor();

    int index = graphsList.indexOf(active_graph);
    graphsList.removeAt(index);
    active_graph->close();
    graphs--;
    if (index >= graphsList.count())
        index--;

    if (graphs == 0) {
        active_graph = nullptr;
        return;
    }

    active_graph = dynamic_cast<Graph *>(graphsList.at(index));

    for (i = 0; i < (int)graphsList.count(); i++) {
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        if (gr == active_graph) {
            auto *button = dynamic_cast<LayerButton *>(buttonsList.at(i));
            button->setChecked(true);
            break;
        }
    }

    Q_EMIT modifiedPlot();
}

void MultiLayer::setGraphGeometry(int x, int y, int w, int h)
{
    if (active_graph->pos() == QPoint(x, y) && active_graph->size() == QSize(w, h))
        return;

    active_graph->setGeometry(QRect(QPoint(x, y), QSize(w, h)));
    active_graph->plotWidget()->resize(QSize(w, h));
    Q_EMIT modifiedPlot();
}

QSize MultiLayer::arrangeLayers(bool userSize)
{
    const QRect rect = canvas->geometry();

    gsl_vector *xTopR =
            gsl_vector_calloc(graphs); // ratio between top axis + title and canvas height
    gsl_vector *xBottomR = gsl_vector_calloc(graphs); // ratio between bottom axis and canvas height
    gsl_vector *yLeftR = gsl_vector_calloc(graphs);
    gsl_vector *yRightR = gsl_vector_calloc(graphs);
    gsl_vector *maxXTopHeight = gsl_vector_calloc(rows); // maximum top axis + title height in a row
    gsl_vector *maxXBottomHeight = gsl_vector_calloc(rows); // maximum bottom axis height in a row
    gsl_vector *maxYLeftWidth = gsl_vector_calloc(cols); // maximum left axis width in a column
    gsl_vector *maxYRightWidth = gsl_vector_calloc(cols); // maximum right axis width in a column
    gsl_vector *Y = gsl_vector_calloc(rows);
    gsl_vector *X = gsl_vector_calloc(cols);

    int i = 0;
    for (i = 0; i < graphs; i++) { // calculate scales/canvas dimensions reports for each layer and
                                   // stores them in the above vectors
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        QwtPlot *plot = gr->plotWidget();
        QwtPlotLayout *plotLayout = plot->plotLayout();
        QRect cRect = plotLayout->canvasRect().toRect();
        auto ch = (double)cRect.height();
        auto cw = (double)cRect.width();

        QRect tRect = plotLayout->titleRect().toRect();
        auto *scale = dynamic_cast<QwtScaleWidget *>(plot->axisWidget(QwtPlot::xTop));

        int topHeight = 0;
        if (!tRect.isNull())
            topHeight += tRect.height() + plotLayout->spacing();
        if (scale) {
            QRect sRect = plotLayout->scaleRect(QwtPlot::xTop).toRect();
            topHeight += sRect.height();
        }
        gsl_vector_set(xTopR, i, double(topHeight) / ch);

        scale = dynamic_cast<QwtScaleWidget *>(plot->axisWidget(QwtPlot::xBottom));
        if (scale) {
            QRect sRect = plotLayout->scaleRect(QwtPlot::xBottom).toRect();
            gsl_vector_set(xBottomR, i, double(sRect.height()) / ch);
        }

        scale = dynamic_cast<QwtScaleWidget *>(plot->axisWidget(QwtPlot::yLeft));
        if (scale) {
            QRect sRect = plotLayout->scaleRect(QwtPlot::yLeft).toRect();
            gsl_vector_set(yLeftR, i, double(sRect.width()) / cw);
        }

        scale = dynamic_cast<QwtScaleWidget *>(plot->axisWidget(QwtPlot::yRight));
        if (scale) {
            QRect sRect = plotLayout->scaleRect(QwtPlot::yRight).toRect();
            gsl_vector_set(yRightR, i, double(sRect.width()) / cw);
        }

        // calculate max scales/canvas dimensions ratio for each line and column and stores them to
        // vectors
        int row = i / cols;
        if (row >= rows)
            row = rows - 1;

        int col = i % cols;

        double aux = gsl_vector_get(xTopR, i);
        double old_max = gsl_vector_get(maxXTopHeight, row);
        if (aux >= old_max)
            gsl_vector_set(maxXTopHeight, row, aux);

        aux = gsl_vector_get(xBottomR, i);
        if (aux >= gsl_vector_get(maxXBottomHeight, row))
            gsl_vector_set(maxXBottomHeight, row, aux);

        aux = gsl_vector_get(yLeftR, i);
        if (aux >= gsl_vector_get(maxYLeftWidth, col))
            gsl_vector_set(maxYLeftWidth, col, aux);

        aux = gsl_vector_get(yRightR, i);
        if (aux >= gsl_vector_get(maxYRightWidth, col))
            gsl_vector_set(maxYRightWidth, col, aux);
    }

    double c_heights = 0.0;
    for (i = 0; i < rows; i++) {
        gsl_vector_set(Y, i, c_heights);
        c_heights += 1 + gsl_vector_get(maxXTopHeight, i) + gsl_vector_get(maxXBottomHeight, i);
    }

    double c_widths = 0.0;
    for (i = 0; i < cols; i++) {
        gsl_vector_set(X, i, c_widths);
        c_widths += 1 + gsl_vector_get(maxYLeftWidth, i) + gsl_vector_get(maxYRightWidth, i);
    }

    if (!userSize) {
        l_canvas_width = int((rect.width() - (cols - 1) * colsSpace - right_margin - left_margin)
                             / c_widths);
        l_canvas_height = int((rect.height() - (rows - 1) * rowsSpace - top_margin - bottom_margin)
                              / c_heights);
    }

    QSize size = QSize(l_canvas_width, l_canvas_height);

    for (i = 0; i < graphs; i++) {
        int row = i / cols;
        if (row >= rows)
            row = rows - 1;

        int col = i % cols;

        // calculate sizes and positions for layers
        const int w =
                int(l_canvas_width * (1 + gsl_vector_get(yLeftR, i) + gsl_vector_get(yRightR, i)));
        const int h =
                int(l_canvas_height * (1 + gsl_vector_get(xTopR, i) + gsl_vector_get(xBottomR, i)));

        int x = left_margin + col * colsSpace;
        if (hor_align == HCenter)
            x += int(l_canvas_width
                     * (gsl_vector_get(X, col) + gsl_vector_get(maxYLeftWidth, col)
                        - gsl_vector_get(yLeftR, i)));
        else if (hor_align == Left)
            x += int(l_canvas_width * gsl_vector_get(X, col));
        else if (hor_align == Right)
            x += int(l_canvas_width
                     * (gsl_vector_get(X, col) + gsl_vector_get(maxYLeftWidth, col)
                        - gsl_vector_get(yLeftR, i) + gsl_vector_get(maxYRightWidth, col)
                        - gsl_vector_get(yRightR, i)));

        int y = top_margin + row * rowsSpace;
        if (vert_align == VCenter)
            y += int(l_canvas_height
                     * (gsl_vector_get(Y, row) + gsl_vector_get(maxXTopHeight, row)
                        - gsl_vector_get(xTopR, i)));
        else if (vert_align == Top)
            y += int(l_canvas_height * gsl_vector_get(Y, row));
        else if (vert_align == Bottom)
            y += int(l_canvas_height
                     * (gsl_vector_get(Y, row) + gsl_vector_get(maxXTopHeight, row)
                        - gsl_vector_get(xTopR, i) + +gsl_vector_get(maxXBottomHeight, row)
                        - gsl_vector_get(xBottomR, i)));

        // resizes and moves layers
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        bool autoscaleFonts = false;
        if (!userSize) { // When the user specifies the layer canvas size, the window is resized
            // and the fonts must be scaled accordingly. If the size is calculated
            // automatically we don't rescale the fonts in order to prevent problems
            // with too small fonts when the user adds new layers or when removing layers

            autoscaleFonts = gr->autoscaleFonts(); // save user settings
            gr->setAutoscaleFonts(false);
        }

        gr->setGeometry(QRect(x, y, w, h));
        gr->plotWidget()->resize(QSize(w, h));

        if (!userSize)
            gr->setAutoscaleFonts(autoscaleFonts); // restore user settings
    }

    // free memory
    gsl_vector_free(maxXTopHeight);
    gsl_vector_free(maxXBottomHeight);
    gsl_vector_free(maxYLeftWidth);
    gsl_vector_free(maxYRightWidth);
    gsl_vector_free(xTopR);
    gsl_vector_free(xBottomR);
    gsl_vector_free(yLeftR);
    gsl_vector_free(yRightR);
    gsl_vector_free(X);
    gsl_vector_free(Y);
    return size;
}

void MultiLayer::findBestLayout(int &rows, int &cols)
{
    int NumGraph = graphs;
    if (NumGraph % 2 == 0) // NumGraph is an even number
    {
        if (NumGraph <= 2)
            cols = NumGraph / 2 + 1;
        else // if (NumGraph > 2)
            cols = NumGraph / 2;

        if (NumGraph < 8)
            rows = NumGraph / 4 + 1;
        if (NumGraph >= 8)
            rows = NumGraph / 4;
    } else // if (NumGraph % 2 != 0) // NumGraph is an odd number
    {
        int Num = NumGraph + 1;

        if (Num <= 2)
            cols = 1;
        else // if (Num > 2)
            cols = Num / 2;

        if (Num < 8)
            rows = Num / 4 + 1;
        if (Num >= 8)
            rows = Num / 4;
    }
}

void MultiLayer::arrangeLayers(bool fit, bool userSize)
{
    if (!graphs)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    if (d_layers_selector)
        delete d_layers_selector;

    if (fit)
        findBestLayout(rows, cols);

    // the canvas sizes of all layers become equal only after several
    // resize iterations, due to the way Qwt handles the plot layout
    int iterations = 0;
    QSize size = arrangeLayers(userSize);
    QSize canvas_size = QSize(1, 1);
    while (canvas_size != size && iterations < 10) {
        iterations++;
        canvas_size = size;
        size = arrangeLayers(userSize);
    }

    if (userSize) { // resize window
        bool ignoreResize = active_graph->ignoresResizeEvents();
        for (int i = 0; i < (int)graphsList.count(); i++) {
            auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
            gr->setIgnoreResizeEvents(true);
        }

        this->showNormal();
        QSize canvas_rect_size = canvas->childrenRect().size();
        this->resize(QSize(canvas_rect_size.width() + right_margin,
                           canvas_rect_size.height() + bottom_margin + LayerButton::btnSize()));

        for (int i = 0; i < (int)graphsList.count(); i++) {
            auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
            gr->setIgnoreResizeEvents(ignoreResize);
        }
    }

    Q_EMIT modifiedPlot();
    QApplication::restoreOverrideCursor();
}

void MultiLayer::setCols(int c)
{
    cols = c;
}

void MultiLayer::setRows(int r)
{
    rows = r;
}

void MultiLayer::exportToFile(const QString &fileName)
{
    if (fileName.isEmpty()) {
        QMessageBox::critical(nullptr, tr("Error"), tr("Please provide a valid file name!"));
        return;
    }

    if (fileName.contains(".eps") || fileName.contains(".pdf") || fileName.contains(".ps")) {
        exportVector(fileName);
        return;
    } else if (fileName.contains(".svg")) {
        exportSVG(fileName);
        return;
    } else {
        QList<QByteArray> list = QImageWriter::supportedImageFormats();
        for (int i = 0; i < list.count(); i++) {
            if (fileName.contains("." + list[i].toLower())) {
                exportImage(fileName);
                return;
            }
        }
        QMessageBox::critical(this, tr("Error"), tr("File format not handled, operation aborted!"));
    }
}

void MultiLayer::exportImage(const QString &fileName, int quality)
{
    QImage image(canvas->size(), QImage::Format_ARGB32);
    exportPainter(image);
    image.save(fileName, nullptr, quality);
}

void MultiLayer::exportPDF(const QString &fname)
{
    exportVector(fname);
}

void MultiLayer::exportVector(const QString &fileName, int, bool color, bool keepAspect,
                              QPageSize pageSize, QPageLayout::Orientation orientation)
{
    if (fileName.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Please provide a valid file name!"));
        return;
    }

    QPrinter printer;
    printer.setDocName(this->name());
    printer.setCreator("Makhber");
    printer.setFullPage(true);
    printer.setOutputFileName(fileName);
    if (fileName.contains(".eps")) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Output in postscript format is not available for Qt5, using PDF"));
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(fileName + ".pdf");
    }

    if (color)
        printer.setColorMode(QPrinter::Color);
    else
        printer.setColorMode(QPrinter::GrayScale);

    if (pageSize == QPageSize(QPageSize::Custom))
        printer.setPageSize(QPageSize(canvas->size(), QPageSize::Point));
    else {
        printer.setPageOrientation(orientation);
        printer.setPageSize(pageSize);
    }

    exportPainter(printer, keepAspect);
}

void MultiLayer::exportSVG(const QString &fname)
{
    QSvgGenerator generator;
    generator.setFileName(fname);
    generator.setSize(canvas->size());
    generator.setViewBox(QRect(QPoint(0, 0), generator.size()));
    generator.setResolution(96); // FIXME hardcored
    generator.setTitle(this->name());
    exportPainter(generator);
}

void MultiLayer::exportPainter(QPaintDevice &paintDevice, bool keepAspect, QRect rect)
{
    QPainter p(&paintDevice);
    exportPainter(p, keepAspect, rect, QSize(paintDevice.width(), paintDevice.height()));
    p.end();
}

void MultiLayer::exportPainter(QPainter &painter, bool keepAspect, QRect rect, QSize size)
{
    if (size == QSize())
        size = canvas->size();
    if (rect == QRect())
        rect = canvas->rect();
    if (keepAspect) {
        QSize scaled = rect.size();
        scaled.scale(size, Qt::KeepAspectRatio);
        size = scaled;
    }
    painter.scale((double)size.width() / (double)rect.width(),
                  (double)size.height() / (double)rect.height());

    painter.fillRect(rect, widget()->palette().brush(backgroundRole()));

    for (int i = 0; i < (int)graphsList.count(); i++) {
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        Plot *myPlot = dynamic_cast<Plot *>(gr->plotWidget());

        QPoint pos = QPoint(gr->pos().x(), gr->pos().y());
        gr->exportPainter(painter, false, QRect(pos, myPlot->size()));
    }
}

void MultiLayer::copyAllLayers()
{
    QImage image(canvas->size(), QImage::Format_ARGB32);
    exportPainter(image);
    QApplication::clipboard()->setImage(image);
}

void MultiLayer::printActiveLayer()
{
    if (active_graph) {
        active_graph->setScaleOnPrint(d_scale_on_print);
        active_graph->printCropmarks(d_print_cropmarks);
        active_graph->print();
    }
}

void MultiLayer::print()
{
    QPrinter printer;
    printer.setColorMode(QPrinter::Color);
    printer.setFullPage(true);
    QRect canvasRect = canvas->rect();
    double aspect = double(canvasRect.width()) / double(canvasRect.height());
    if (aspect < 1)
        printer.setPageOrientation(QPageLayout::Portrait);
    else
        printer.setPageOrientation(QPageLayout::Landscape);

    QPrintDialog printDialog(&printer);
    if (printDialog.exec() == QDialog::Accepted) {
        QPainter paint(&printer);
        printAllLayers(&paint);
        paint.end();
    }
}

void MultiLayer::printAllLayers(QPainter *painter)
{
    if (!painter)
        return;

    auto *printer = dynamic_cast<QPrinter *>(painter->device());
    QRect canvasRect = canvas->rect();
    QRect pageRect = printer->pageLayout().fullRectPixels(printer->resolution());
    QRect cr = canvasRect; // cropmarks rectangle

    if (d_scale_on_print) {
        int margin = (int)((1 / 2.54) * printer->logicalDpiY()); // 1 cm margins
        double scaleFactorX = (double)(pageRect.width() - 2 * margin) / (double)canvasRect.width();
        double scaleFactorY =
                (double)(pageRect.height() - 2 * margin) / (double)canvasRect.height();

        if (d_print_cropmarks) {
            cr.moveTo(QPoint(margin + int(cr.x() * scaleFactorX),
                             margin + int(cr.y() * scaleFactorY)));
            cr.setWidth(int(cr.width() * scaleFactorX));
            cr.setHeight(int(cr.height() * scaleFactorX));
        }

        for (int i = 0; i < (int)graphsList.count(); i++) {
            auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
            Plot *myPlot = gr->plotWidget();

            QPoint pos = gr->pos();
            pos = QPoint(margin + int(pos.x() * scaleFactorX),
                         margin + int(pos.y() * scaleFactorY));

            int width = int(myPlot->frameGeometry().width() * scaleFactorX);
            int height = int(myPlot->frameGeometry().height() * scaleFactorY);

            gr->print(painter, QRect(pos, QSize(width, height)));
        }
    } else {
        int x_margin = (pageRect.width() - canvasRect.width()) / 2;
        int y_margin = (pageRect.height() - canvasRect.height()) / 2;

        if (d_print_cropmarks)
            cr.moveTo(x_margin, y_margin);

        for (int i = 0; i < (int)graphsList.count(); i++) {
            auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
            Plot *myPlot = dynamic_cast<Plot *>(gr->plotWidget());

            QPoint pos = gr->pos();
            pos = QPoint(x_margin + pos.x(), y_margin + pos.y());
            gr->print(painter, QRect(pos, myPlot->size()));
        }
    }
    if (d_print_cropmarks) {
        cr.adjust(-1, -1, 2, 2);
        painter->save();
        painter->setPen(QPen(QColor(Qt::black), 0.5, Qt::DashLine));
        painter->drawLine(pageRect.left(), cr.top(), pageRect.right(), cr.top());
        painter->drawLine(pageRect.left(), cr.bottom(), pageRect.right(), cr.bottom());
        painter->drawLine(cr.left(), pageRect.top(), cr.left(), pageRect.bottom());
        painter->drawLine(cr.right(), pageRect.top(), cr.right(), pageRect.bottom());
        painter->restore();
    }
}

void MultiLayer::setFonts(const QFont &titleFnt, const QFont &scaleFnt, const QFont &numbersFnt,
                          const QFont &legendFnt)
{
    for (int i = 0; i < (int)graphsList.count(); i++) {
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        QwtPlot *plot = gr->plotWidget();

        QwtText text = plot->title();
        text.setFont(titleFnt);
        plot->setTitle(text);
        for (int j = 0; j < QwtPlot::axisCnt; j++) {
            plot->setAxisFont(j, numbersFnt);

            text = plot->axisTitle(j);
            text.setFont(scaleFnt);
            plot->setAxisTitle(j, text);
        }

        QVector<int> keys = gr->textMarkerKeys();
        for (int key : keys) {
            auto *mrk = dynamic_cast<Legend *>(gr->textMarker(key));
            if (mrk)
                mrk->setFont(legendFnt);
        }
        plot->replot();
    }
    Q_EMIT modifiedPlot();
}

void MultiLayer::connectLayer(Graph *g)
{
    connect(g, SIGNAL(drawLineEnded(bool)), this, SIGNAL(drawLineEnded(bool)));
    connect(g, SIGNAL(drawTextOff()), this, SIGNAL(drawTextOff()));
    connect(g, SIGNAL(showPlotDialog(int)), this, SIGNAL(showPlotDialog(int)));
    connect(g, SIGNAL(createTable(const QString &, const QString &, QList<Column *>)), this,
            SIGNAL(createTable(const QString &, const QString &, QList<Column *>)));
    connect(g, SIGNAL(viewLineDialog()), this, SIGNAL(showLineDialog()));
    connect(g, SIGNAL(showContextMenu()), this, SIGNAL(showGraphContextMenu()));
    connect(g, SIGNAL(showLayerButtonContextMenu()), this, SIGNAL(showLayerButtonContextMenu()));
    connect(g, SIGNAL(showSelectedAxisDialog(int)), this, SIGNAL(showSelectedAxisDialog(int)));
    connect(g, SIGNAL(axisDblClicked(int)), this, SIGNAL(showScaleDialog(int)));
    connect(g, SIGNAL(xAxisTitleDblClicked()), this, SIGNAL(showXAxisTitleDialog()));
    connect(g, SIGNAL(yAxisTitleDblClicked()), this, SIGNAL(showYAxisTitleDialog()));
    connect(g, SIGNAL(rightAxisTitleDblClicked()), this, SIGNAL(showRightAxisTitleDialog()));
    connect(g, SIGNAL(topAxisTitleDblClicked()), this, SIGNAL(showTopAxisTitleDialog()));
    connect(g, SIGNAL(showMarkerPopupMenu()), this, SIGNAL(showMarkerPopupMenu()));
    connect(g, SIGNAL(showCurveContextMenu(int)), this, SIGNAL(showCurveContextMenu(int)));
    connect(g, SIGNAL(cursorInfo(const QString &)), this, SIGNAL(cursorInfo(const QString &)));
    connect(g, SIGNAL(viewImageDialog()), this, SIGNAL(showImageDialog()));
    connect(g, SIGNAL(viewTitleDialog()), this, SIGNAL(viewTitleDialog()));
    connect(g, SIGNAL(modifiedGraph()), this, SIGNAL(modifiedPlot()));
    connect(g, SIGNAL(selectedGraph(Graph *)), this, SLOT(setActiveGraph(Graph *)));
    connect(g, SIGNAL(viewTextDialog()), this, SIGNAL(showTextDialog()));
    connect(g, SIGNAL(createIntensityTable(const QString &)), this,
            SIGNAL(createIntensityTable(const QString &)));
}

void MultiLayer::addTextLayer(int f, const QFont &font, const QColor &textCol,
                              const QColor &backgroundCol)
{
    defaultTextMarkerFont = font;
    defaultTextMarkerFrame = f;
    defaultTextMarkerColor = textCol;
    defaultTextMarkerBackground = backgroundCol;

    addTextOn = true;
    QApplication::setOverrideCursor(Qt::IBeamCursor);
    canvas->grabMouse();
}

void MultiLayer::addTextLayer(const QPoint &pos)
{
    Graph *g = addLayer();
    g->removeLegend();
    g->setTitle("");
    QVector<bool> axesOn(4);
    for (int j = 0; j < 4; j++)
        axesOn[j] = false;
    g->enableAxes(axesOn);
    g->setIgnoreResizeEvents(true);
    g->setTextMarkerDefaults(defaultTextMarkerFrame, defaultTextMarkerFont, defaultTextMarkerColor,
                             defaultTextMarkerBackground);
    Legend *mrk = g->newLegend(tr("enter your text here"));
    QSizeF size = mrk->rect().size();
    setGraphGeometry(pos.x(), pos.y(), size.width() + 10, size.height() + 10);
    g->setIgnoreResizeEvents(false);
    g->show();
    QApplication::restoreOverrideCursor();
    canvas->releaseMouse();
    addTextOn = false;
    Q_EMIT drawTextOff();
    Q_EMIT modifiedPlot();
}

bool MultiLayer::eventFilter(QObject *object, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress && object == dynamic_cast<QObject *>(canvas)) {
        const auto *me = dynamic_cast<const QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton && addTextOn)
            addTextLayer(me->pos());

        return false;
    } else if (e->type() == QEvent::Resize && object == dynamic_cast<QObject *>(canvas)) {
        resizeLayers(dynamic_cast<const QResizeEvent *>(e));
    }
    return MyWidget::eventFilter(object, e);
}

void MultiLayer::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F12) {
        if (d_layers_selector)
            delete d_layers_selector;
        int index = graphsList.indexOf(dynamic_cast<QWidget *>(active_graph)) + 1;
        if (index >= graphsList.size())
            index = 0;
        auto *g = dynamic_cast<Graph *>(graphsList.at(index));
        if (g)
            setActiveGraph(g);
        return;
    }

    if (e->key() == Qt::Key_F10) {
        if (d_layers_selector)
            delete d_layers_selector;
        int index = graphsList.indexOf(dynamic_cast<QWidget *>(active_graph)) - 1;
        if (index < 0)
            index = graphsList.size() - 1;
        auto *g = dynamic_cast<Graph *>(graphsList.at(index));
        if (g)
            setActiveGraph(g);
        return;
    }

    if (e->key() == Qt::Key_F11) {
        Q_EMIT showWindowContextMenu();
        return;
    }
}

void MultiLayer::wheelEvent(QWheelEvent *e)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);

    bool resize = false;
    QPoint aux;
    QSize intSize;
    Graph *resize_graph = nullptr;
    // Get the position of the mouse
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    int xMouse = e->position().x();
    int yMouse = e->position().y();
#else
    int xMouse = e->x();
    int yMouse = e->y();
#endif
    for (int i = 0; i < (int)graphsList.count(); i++) {
        auto *gr = dynamic_cast<Graph *>(graphsList.at(i));
        intSize = gr->plotWidget()->size();
        aux = gr->pos();
        if (xMouse > aux.x() && xMouse < (aux.x() + intSize.width())) {
            if (yMouse > aux.y() && yMouse < (aux.y() + intSize.height())) {
                resize_graph = gr;
                resize = true;
            }
        }
    }
    if (resize
        && (e->modifiers() == Qt::AltModifier || e->modifiers() == Qt::ControlModifier
            || e->modifiers() == Qt::ShiftModifier)) {
        intSize = resize_graph->plotWidget()->size();
        // If Alt is pressed then change the width
        if (e->modifiers() == Qt::AltModifier) {
            if (e->angleDelta().y() > 0) {
                intSize.rwidth() += 5;
            } else if (e->angleDelta().y() < 0) {
                intSize.rwidth() -= 5;
            }
        }
        // If Ctrl is pressed then changed the height
        else if (e->modifiers() == Qt::ControlModifier) {
            if (e->angleDelta().y() > 0) {
                intSize.rheight() += 5;
            } else if (e->angleDelta().y() < 0) {
                intSize.rheight() -= 5;
            }
        }
        // If Shift is pressed then resize
        else if (e->modifiers() == Qt::ShiftModifier) {
            if (e->angleDelta().y() > 0) {
                intSize.rwidth() += 5;
                intSize.rheight() += 5;
            } else if (e->angleDelta().y() < 0) {
                intSize.rwidth() -= 5;
                intSize.rheight() -= 5;
            }
        }

        aux = resize_graph->pos();
        resize_graph->setGeometry(QRect(QPoint(aux.x(), aux.y()), intSize));
        resize_graph->plotWidget()->resize(intSize);

        Q_EMIT modifiedPlot();
    }
    QApplication::restoreOverrideCursor();
}

bool MultiLayer::isEmpty()
{
    if (graphs <= 0)
        return true;
    else
        return false;
}

void MultiLayer::saveToJson(QJsonObject *jsObject, const QJsonObject &jsGeometry)
{
    jsObject->insert("cols", cols);
    jsObject->insert("rows", rows);
    jsObject->insert("creationDate", birthDate());
    jsObject->insert("WindowLabel", windowLabel());
    jsObject->insert("captionPolicy", captionPolicy());

    QJsonObject jsMargins {};
    jsMargins.insert("leftMargin", left_margin);
    jsMargins.insert("rightMargin", right_margin);
    jsMargins.insert("topMargin", top_margin);
    jsMargins.insert("bottomMargin", bottom_margin);
    jsObject->insert("margins", jsMargins);

    QJsonObject jsSpacing {};
    jsSpacing.insert("rowsSpace", rowsSpace);
    jsSpacing.insert("colsSpace", colsSpace);
    jsObject->insert("spacing", jsSpacing);

    QJsonObject jsCanvas {};
    jsCanvas.insert("canvasWidth", l_canvas_width);
    jsCanvas.insert("canvasHeight", l_canvas_height);
    jsObject->insert("layerCanvasSize", jsCanvas);

    QJsonObject jsAlign {};
    jsAlign.insert("horAlign", hor_align);
    jsAlign.insert("vertAlign", vert_align);
    jsObject->insert("alignement", jsAlign);

    QJsonArray jsGraphs {};
    for (int i = 0; i < (int)graphsList.count(); i++) {
        auto *ag = dynamic_cast<Graph *>(graphsList.at(i));
        QJsonObject jsGraph {};
        ag->saveToJson(&jsGraph);
        jsGraphs.append(jsGraph);
    }
    jsObject->insert("graphs", jsGraphs);

    jsObject->insert("name", name());
    jsObject->insert("geometry", jsGeometry);
    jsObject->insert("type", "MultiLayer");
}

void MultiLayer::saveAsTemplate(QJsonObject *jsObject, const QJsonObject &jsGeometry)
{
    jsObject->insert("rows", rows);
    jsObject->insert("cols", cols);
    jsObject->insert("geometry", jsGeometry);

    QJsonObject jsMargins {};
    jsMargins.insert("leftMargin", left_margin);
    jsMargins.insert("rightMargin", right_margin);
    jsMargins.insert("topMargin", top_margin);
    jsMargins.insert("bottomMargin", bottom_margin);
    jsObject->insert("margins", jsMargins);

    QJsonObject jsSpacing {};
    jsSpacing.insert("rowsSpace", rowsSpace);
    jsSpacing.insert("colsSpace", colsSpace);
    jsObject->insert("spacing", jsSpacing);

    QJsonObject jsCanvas {};
    jsCanvas.insert("canvasWidth", l_canvas_width);
    jsCanvas.insert("canvasHeight", l_canvas_height);
    jsObject->insert("layerCanvasSize", jsCanvas);

    QJsonObject jsAlign {};
    jsAlign.insert("horAlign", hor_align);
    jsAlign.insert("vertAlign", vert_align);
    jsObject->insert("alignement", jsAlign);

    QJsonArray jsGraphs {};
    for (int i = 0; i < (int)graphsList.count(); i++) {
        auto *ag = dynamic_cast<Graph *>(graphsList.at(i));
        QJsonObject jsGraph {};
        ag->saveToJson(&jsGraph);
        jsGraphs.append(jsGraph);
    }
    jsObject->insert("graphs", jsGraphs);

    jsObject->insert("templateType", "MultiLayer");
}

void MultiLayer::mousePressEvent(QMouseEvent *e)
{
    if (!this->widget()->geometry().contains(e->pos())) { // event.pos is in titlebar
        if (e->button() == Qt::RightButton) {
            Q_EMIT showTitleBarMenu();
            e->accept();
        } else {
            MyWidget::mousePressEvent(e);
        }
        return;
    }
    int margin = 5;
    QPoint pos = canvas->mapFrom(this, e->pos());
    // iterate backwards, so layers on top are preferred for selection
    QList<QWidget *>::iterator i = graphsList.end();
    while (i != graphsList.begin()) {
        --i;
        QRect igeo = (*i)->frameGeometry();
        igeo.adjust(-margin, -margin, margin, margin);
        if (igeo.contains(pos)) {
            if (e->modifiers() & Qt::ShiftModifier) {
                if (d_layers_selector)
                    d_layers_selector->add(*i);
                else {
                    d_layers_selector = new SelectionMoveResizer(*i);
                    connect(d_layers_selector, SIGNAL(targetsChanged()), this,
                            SIGNAL(modifiedPlot()));
                }
            } else {
                setActiveGraph(dynamic_cast<Graph *>(*i));
                active_graph->raise();
                if (!d_layers_selector) {
                    d_layers_selector = new SelectionMoveResizer(*i);
                    connect(d_layers_selector, SIGNAL(targetsChanged()), this,
                            SIGNAL(modifiedPlot()));
                }
            }
            return;
        }
    }
    if (d_layers_selector)
        delete d_layers_selector;
}

void MultiLayer::setMargins(int lm, int rm, int tm, int bm)
{
    left_margin = lm;
    right_margin = rm;
    top_margin = tm;
    bottom_margin = bm;
}

void MultiLayer::setSpacing(int rgap, int cgap)
{
    rowsSpace = rgap;
    colsSpace = cgap;
}

void MultiLayer::setLayerCanvasSize(int w, int h)
{
    l_canvas_width = w;
    l_canvas_height = h;
}

void MultiLayer::setAlignement(int ha, int va)
{
    hor_align = ha;
    vert_align = va;
}

void MultiLayer::setLayersNumber(int n)
{
    if (graphs == n)
        return;

    int dn = graphs - n;
    if (dn > 0) {
        for (int i = 0; i < dn; i++) { // remove layer buttons
            auto *btn = dynamic_cast<LayerButton *>(buttonsList.last());
            if (btn) {
                btn->close();
                buttonsList.removeLast();
            }

            auto *g = dynamic_cast<Graph *>(graphsList.last());
            if (g) { // remove layers
                if (g->zoomOn() || g->activeTool())
                    setPointerCursor();

                g->close();
                graphsList.removeLast();
            }
        }
        graphs = n;
        if (!graphs) {
            active_graph = nullptr;
            return;
        }

        // check whether the active Graph.has been deleted
        if (graphsList.indexOf(active_graph) == -1)
            active_graph = dynamic_cast<Graph *>(graphsList.last());
        for (int j = 0; j < (int)graphsList.count(); j++) {
            auto *gr = dynamic_cast<Graph *>(graphsList.at(j));
            if (gr == active_graph) {
                auto *button = dynamic_cast<LayerButton *>(buttonsList.at(j));
                button->setChecked(true);
                break;
            }
        }
    } else {
        for (int i = 0; i < abs(dn); i++)
            addLayer();
    }

    Q_EMIT modifiedPlot();
}

void MultiLayer::copy(ApplicationWindow *parent, MultiLayer *ml)
{
    hide(); // FIXME: find a better way to avoid a resize event
    resize(ml->size());

    setSpacing(ml->rowsSpacing(), ml->colsSpacing());
    setAlignement(ml->horizontalAlignement(), ml->verticalAlignement());
    setMargins(ml->leftMargin(), ml->rightMargin(), ml->topMargin(), ml->bottomMargin());

    QWidgetList ml_graphsList = ml->graphPtrs();
    for (int i = 0; i < ml_graphsList.count(); i++) {
        auto *g = dynamic_cast<Graph *>(ml_graphsList.at(i));
        Graph *g2 = addLayer(g->pos().x(), g->pos().y(), g->width(), g->height());
        g2->copy(parent, g);
        g2->setIgnoreResizeEvents(g->ignoresResizeEvents());
        g2->setAutoscaleFonts(g->autoscaleFonts());
    }
    show();
}

bool MultiLayer::focusNextPrevChild(bool next)
{
    if (!active_graph)
        return true;

    return active_graph->focusNextPrevChild(next);
}
