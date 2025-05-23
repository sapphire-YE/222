#include "DrawingArea.h"
#include "ShapeFactory.h"
#include <QDataStream>
#include <QSvgGenerator>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSvgGenerator>
#include <QWheelEvent> // 添加对滚轮事件的支持
#include <algorithm>

DrawingArea::DrawingArea(QWidget *parent) : QWidget(parent)
{
    setObjectName("drawingArea");
    setFocusPolicy(Qt::StrongFocus); // 允许接收键盘事件
    setAcceptDrops(true);            // 允许接收拖拽
    setMouseTracking(true);
    createContextMenu(); // 创建右键菜单
}

DrawingArea::~DrawingArea()
{
    // 清理资源
    if (m_textEdit)
    {
        delete m_textEdit;
        m_textEdit = nullptr;
    }
    if (m_contextMenu)
    {
        delete m_contextMenu;
        m_contextMenu = nullptr;
    }
}

bool DrawingArea::saveToFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    QJsonArray shapesArray;
    for (const auto &shape : shapes)
    {
        QJsonObject shapeObj = shape->toJson();
        shapesArray.append(shapeObj);
    }

    QJsonObject rootObj;
    rootObj["shapes"] = shapesArray;
    rootObj["backgroundColor"] = m_bgColor.name();
    rootObj["gridSize"] = m_gridSize;
    rootObj["size"] = QJsonObject{
        {"width", width()},
        {"height", height()}};

    // 保存箭头连接信息
    QJsonArray connectionsArray;
    for (const auto &conn : arrowConnections)
    {
        QJsonObject connObj;
        connObj["arrowIndex"] = conn.arrowIndex;
        connObj["shapeIndex"] = conn.shapeIndex;
        connObj["handleIndex"] = conn.handleIndex;
        connObj["isStartPoint"] = conn.isStartPoint;
        connectionsArray.append(connObj);
    }
    rootObj["connections"] = connectionsArray;

    QJsonDocument doc(rootObj);
    file.write(doc.toJson());
    return true;
}

bool DrawingArea::loadFromFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull())
    {
        return false;
    }

    QJsonObject rootObj = doc.object();

    // 清空当前画布
    clear();

    // 恢复背景色和网格大小
    m_bgColor = QColor(rootObj["backgroundColor"].toString());
    m_gridSize = rootObj["gridSize"].toInt();

    // 恢复画布大小
    QJsonObject sizeObj = rootObj["size"].toObject();
    setPageSize(QSize(sizeObj["width"].toInt(), sizeObj["height"].toInt()));

    // 恢复所有图形
    QJsonArray shapesArray = rootObj["shapes"].toArray();
    for (const QJsonValue &shapeVal : shapesArray)
    {
        QJsonObject shapeObj = shapeVal.toObject();
        QString type = shapeObj["type"].toString();

        std::unique_ptr<ShapeBase> shape;
        if (type == "rect")
        {
            shape = ShapeFactory::createRect(QRect());
        }
        else if (type == "ellipse")
        {
            shape = ShapeFactory::createEllipse(QRect());
        }
        else if (type == "arrow")
        {
            shape = ShapeFactory::createArrow(QLine());
        }
        else if (type == "pentagon")
        {
            shape = ShapeFactory::createPentagon(QRect());
        }
        else if (type == "triangle")
        {
            shape = ShapeFactory::createTriangle(QRect());
        }
        else if (type == "diamond")
        {
            shape = ShapeFactory::createDiamond(QRect());
        }
        else if (type == "roundedrect")
        {
            shape = ShapeFactory::createRoundedRect(QRect());
        }
        // else if (shapeType == "polygon") {
        //     // 待实现
        // }

        if (shape)
        {
            shape->fromJson(shapeObj);
            shapes.push_back(std::move(shape));
        }
    }

    // 恢复箭头连接
    QJsonArray connectionsArray = rootObj["connections"].toArray();
    for (const QJsonValue &connVal : connectionsArray)
    {
        QJsonObject connObj = connVal.toObject();
        ArrowConnection conn;
        conn.arrowIndex = connObj["arrowIndex"].toInt();
        conn.shapeIndex = connObj["shapeIndex"].toInt();
        conn.handleIndex = connObj["handleIndex"].toInt();
        conn.isStartPoint = connObj["isStartPoint"].toBool();
        arrowConnections.push_back(conn);
    }

    update();
    return true;
}

void DrawingArea::clear()
{
    shapes.clear();
    arrowConnections.clear();
    selectedIndex = -1;
    snappedHandle = SnapInfo();

    // 清空撤销重做栈
    while (!m_undoStack.empty())
    {
        m_undoStack.pop();
    }
    while (!m_redoStack.empty())
    {
        m_redoStack.pop();
    }
    emit canUndoChanged(false);
    emit canRedoChanged(false);

    update();
}

bool DrawingArea::exportToPNG(const QString &fileName)
{
    QImage image(size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制背景
    painter.fillRect(rect(), m_bgColor);

    // 绘制所有图形
    for (const auto &shape : shapes)
    {
        shape->paint(&painter, false); // 不显示控制点
    }

    return image.save(fileName, "PNG");
}

bool DrawingArea::exportToSVG(const QString &fileName)
{
    QSvgGenerator generator;
    generator.setFileName(fileName);
    generator.setSize(size());
    generator.setViewBox(rect());
    generator.setTitle("Flow Chart");
    generator.setDescription("Generated by Flow Chart Editor");

    QPainter painter;
    painter.begin(&generator);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制背景
    painter.fillRect(rect(), m_bgColor);

    // 绘制所有图形
    for (const auto &shape : shapes)
    {
        shape->paint(&painter, false); // 不显示控制点
    }

    painter.end();
    return true;
}

void DrawingArea::setGridSize(int size)
{
    if (size != m_gridSize && size > 0)
    {
        m_gridSize = size;
        emit gridSizeChanged(size);
        update(); // 更新显示
    }
}

void DrawingArea::setPageSize(const QSize &size)
{
    if (size != m_pageSize && size.width() > 0 && size.height() > 0)
    {
        m_pageSize = size;
        emit pageSizeChanged(size);
        update(); // 更新显示
    }
}

void DrawingArea::setGridVisible(bool visible)
{
    if (visible != m_gridVisible)
    {
        m_gridVisible = visible;
        emit gridVisibilityChanged(visible);
        update(); // 更新显示
    }
}

void DrawingArea::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    // 填充工作区背景（页面外区域）为浅灰色，更容易区分页面和工作区
    painter.fillRect(rect(), QColor(240, 240, 240));

    painter.setRenderHint(QPainter::Antialiasing); // 抗锯齿

    // 首先绘制页面边界
    QRect pageRect(0, 0, m_pageSize.width(), m_pageSize.height());
    QRect scaledPageRect = docToScreen(pageRect);

    // 绘制页面背景
    painter.fillRect(scaledPageRect, m_bgColor); // 使用设置的背景颜色

    // 绘制页面边框，便于识别页面边界
    QPen pageBorderPen(QColor(180, 180, 180), 1);
    painter.setPen(pageBorderPen);
    painter.drawRect(scaledPageRect);

    // 应用缩放变换，用于绘制网格和内容
    painter.scale(m_zoomFactor, m_zoomFactor);

    // 画网格，但只在页面区域内绘制
    if (m_gridVisible)
    {                                                           // 只在网格可见时绘制
        int gridSize = m_gridSize;                              // 网格间距
        int majorGridStep = 5;                                  // 每5格一条粗线
        QPen thinPen(QColor(200, 200, 200), 1 / m_zoomFactor);  // 细线浅灰色，保持线宽不变
        QPen thickPen(QColor(120, 120, 120), 2 / m_zoomFactor); // 粗线深灰色，保持线宽不变

        // 页面区域就是网格绘制的范围
        QRect pageDocRect(0, 0, m_pageSize.width(), m_pageSize.height());

        // 第一步：绘制细的竖线
        int startX = 0;                // 从页面左边开始
        int endX = m_pageSize.width(); // 到页面右边结束
        for (int x = startX, idx = startX / gridSize; x <= endX; x += gridSize, ++idx)
        {
            if (idx % majorGridStep != 0) // 只绘制细线
            {
                painter.setPen(thinPen);
                painter.drawLine(x, 0, x, m_pageSize.height()); // 从页面顶部到底部
            }
        }

        // 第二步：绘制所有横线（粗细都绘制）
        int startY = 0;                 // 从页面顶部开始
        int endY = m_pageSize.height(); // 到页面底部结束
        for (int y = startY, idx = startY / gridSize; y <= endY; y += gridSize, ++idx)
        {
            if (idx % majorGridStep == 0)
            {
                painter.setPen(thickPen);
            }
            else
            {
                painter.setPen(thinPen);
            }
            painter.drawLine(0, y, m_pageSize.width(), y); // 从页面左边到右边
        }

        // 第三步：绘制粗的竖线
        for (int x = startX, idx = startX / gridSize; x <= endX; x += gridSize, ++idx)
        {
            if (idx % majorGridStep == 0) // 只绘制粗线
            {
                painter.setPen(thickPen);
                painter.drawLine(x, 0, x, m_pageSize.height()); // 从页面顶部到底部
            }
        }
    }

    // 画图形
    for (int i = 0; i < shapes.size(); ++i)
    {
        bool showHandles = false;
        if (i == snappedHandle.shapeIndex)
            showHandles = true;
        if (i == selectedIndex)
        {
            auto *arrow = dynamic_cast<ShapeArrow *>(shapes[i].get());
            if (arrow && shapes[i]->isHandleSelected())
                showHandles = false;
            else
                showHandles = true;
        }
        shapes[i]->paint(&painter, showHandles);
    }

    QWidget::paintEvent(event); // 调用父类paintEvent
}

void DrawingArea::mousePressEvent(QMouseEvent *event)
{
    // 转换屏幕坐标到文档坐标
    QPoint docPos = screenToDoc(event->pos());

    if (selectedIndex != -1)
    {
        // 检查是否点击了锚点
        const auto &handles = shapes[selectedIndex]->getHandles();
        for (size_t i = 0; i < handles.size(); ++i)
        {
            // 直接在文档坐标系中检查
            if (handles[i].rect.contains(docPos))
            {
                // 检查是否是Arrow类型的锚点（加号锚点）
                if (handles[i].type == ShapeBase::Handle::Arrow)
                {
                    // 创建一个新箭头，将其起点设置在对应的ArrowAnchor位置
                    int direction = handles[i].direction;
                    int arrowAnchorIndex = shapes[selectedIndex]->mapArrowHandleToAnchor(direction);

                    if (arrowAnchorIndex >= 0) // 检查索引是否有效
                    {
                        // 获取对应边缘的ArrowAnchor锚点
                        const auto &arrowAnchors = shapes[selectedIndex]->getArrowAnchors();
                        if (!arrowAnchors.empty() && arrowAnchorIndex < arrowAnchors.size())
                        {
                            QPoint anchorPos = arrowAnchors[arrowAnchorIndex].rect.center();

                            // 创建新箭头，起点在ArrowAnchor锚点位置，终点跟随鼠标
                            QLine arrowLine(anchorPos, docPos);
                            std::unique_ptr<ShapeBase> arrow = ShapeFactory::createArrow(arrowLine);

                            // 存储当前选中图形的索引(创建新箭头前)
                            int originalSelectedIndex = selectedIndex;

                            // 添加到图形列表并选中新箭头
                            shapes.push_back(std::move(arrow));
                            selectedIndex = shapes.size() - 1;

                            // 记录添加图形到历史
                            recordAddShape(selectedIndex);

                            // 记录选中箭头的终点锚点并开始拖动
                            auto *arrowShape = dynamic_cast<ShapeArrow *>(shapes[selectedIndex].get());
                            if (arrowShape)
                            {
                                arrowShape->setSelectedHandleIndex(1); // 选中终点锚点

                                // 记录ArrowAnchor的信息，等待释放鼠标时创建连接
                                snappedHandle = {originalSelectedIndex, arrowAnchorIndex, anchorPos};

                                // 创建新的连接关系
                                ArrowConnection connection;
                                connection.arrowIndex = shapes.size() - 1;     // 新箭头的索引
                                connection.shapeIndex = originalSelectedIndex; // 连接到原始选中的图形
                                connection.handleIndex = arrowAnchorIndex;
                                connection.isStartPoint = true; // 箭头的起点
                                arrowConnections.push_back(connection);

                                // 确保起点锚点正确
                                const auto &anchors = shapes[originalSelectedIndex]->getArrowAnchors();
                                if (arrowAnchorIndex < anchors.size())
                                {
                                    QPoint anchorPos = anchors[arrowAnchorIndex].rect.center();
                                    arrowShape->setP1(anchorPos);
                                    lastMousePos = docPos;
                                    updateConnectedArrows(originalSelectedIndex, QPoint());
                                }
                            }

                            dragging = true;
                            lastMousePos = docPos;
                            update();
                            return;
                        }
                    }
                }
                else
                {
                    // 处理其他类型的锚点，使用原有逻辑
                    shapes[selectedIndex]->setSelectedHandleIndex(i);

                    // 如果是缩放锚点，记录原始大小
                    if (handles[i].type == ShapeBase::Handle::Scale)
                    {
                        resizing = true;
                        originalRect = shapes[selectedIndex]->getRect();
                    }

                    lastMousePos = docPos; // 保存文档坐标
                    dragging = true;
                    update();
                    return;
                }
            }
        }
    }

    // 检查是否点击了某个图形（原有逻辑）
    int oldSelectedIndex = selectedIndex;
    selectedIndex = -1;
    for (int i = shapes.size() - 1; i >= 0; --i)
    {
        // 直接在文档坐标系中检查
        if (shapes[i]->contains(docPos))
        {
            selectedIndex = i;
            lastMousePos = docPos; // 保存文档坐标

            // 记录按下时的起始位置，用于计算总移动距离
            m_moveStartPos = docPos;
            dragging = true;

            update();

            // 发出选中图形信号
            if (oldSelectedIndex != selectedIndex)
            {
                emit shapeSelected(shapes[selectedIndex].get());
            }
            return;
        }
    }

    // 如果之前有选中的图形，现在取消了，发出信号
    if (oldSelectedIndex != -1)
    {
        emit selectionCleared();
    }

    update();
}

void DrawingArea::mouseMoveEvent(QMouseEvent *event)
{
    // 转换屏幕坐标到文档坐标
    QPoint docPos = screenToDoc(event->pos());

    if (dragging && selectedIndex != -1)
    {
        if (shapes[selectedIndex]->isHandleSelected())
        {
            if (auto *arrow = dynamic_cast<ShapeArrow *>(shapes[selectedIndex].get()))
            {
                int handleIndex = arrow->getSelectedHandleIndex();
                if (handleIndex != -1) // 0表示起点，1表示终点
                {
                    QPoint mousePos = docPos; // 使用文档坐标
                    QPoint otherPos;

                    // 确定另一端点的位置
                    if (handleIndex == 0)
                    {
                        otherPos = arrow->getLine().p2();
                    }
                    else
                    {
                        otherPos = arrow->getLine().p1();
                    }

                    const int snapDistance = 10 / m_zoomFactor; // 缩放调整吸附距离
                    bool foundSnap = false;
                    QPoint snapTarget;
                    int snapShapeIndex = -1;
                    int snapHandleIndex = -1;

                    // 检查所有图形的锚点
                    for (size_t i = 0; i < shapes.size(); ++i)
                    {
                        if (i == selectedIndex)
                            continue;
                        if (dynamic_cast<ShapeArrow *>(shapes[i].get()))
                            continue;
                        const auto &arrowAnchors = shapes[i]->getArrowAnchors();
                        for (size_t j = 0; j < arrowAnchors.size(); ++j)
                        {
                            if (arrowAnchors[j].type != ShapeBase::Handle::ArrowAnchor)
                                continue;
                            QPoint target = arrowAnchors[j].rect.center();
                            if (target == otherPos)
                                continue;
                            if ((mousePos - target).manhattanLength() <= snapDistance)
                            {
                                snapTarget = target;
                                snapShapeIndex = i;
                                snapHandleIndex = j;
                                foundSnap = true;
                                break;
                            }
                        }
                        if (foundSnap)
                            break;
                    }

                    if (foundSnap)
                    {
                        // 锁定端点到找到的锚点
                        if (handleIndex == 0)
                            arrow->setP1(snapTarget);
                        else
                            arrow->setP2(snapTarget);

                        // 更新临时吸附信息
                        snappedHandle = {snapShapeIndex, snapHandleIndex, snapTarget};
                    }
                    else
                    {
                        // 没有吸附，端点跟随鼠标
                        if (handleIndex == 0)
                            arrow->setP1(mousePos);
                        else
                            arrow->setP2(mousePos);

                        // 清除吸附信息
                        snappedHandle = {-1, -1, QPoint()};
                    }
                    update();
                }
            }
            else
            {
                // 处理非箭头图形的缩放或旋转
                // 所有操作都在文档坐标系中进行
                QPoint delta = docPos - lastMousePos; // 使用文档坐标计算偏移
                bool isRotating = false;

                // 检查是否在旋转操作
                if (selectedIndex != -1)
                {
                    const auto &handles = shapes[selectedIndex]->getHandles();
                    int selectedHandleIndex = shapes[selectedIndex]->getSelectedHandleIndex();

                    if (selectedHandleIndex != -1 && selectedHandleIndex < handles.size() &&
                        handles[selectedHandleIndex].type == ShapeBase::Handle::Rotate)
                    {
                        isRotating = true;
                    }
                }

                // 处理锚点交互（缩放或旋转）
                shapes[selectedIndex]->handleAnchorInteraction(docPos, lastMousePos);

                // 无论是缩放还是旋转，都需要更新连接的箭头
                // 对于旋转，delta无意义，因为我们会在updateConnectedArrows中通过锚点直接更新
                updateConnectedArrows(selectedIndex, delta);

                lastMousePos = docPos; // 更新为当前文档坐标
                update();
            }
        }
        else
        {
            // 图形整体拖动
            QPoint delta = docPos - lastMousePos; // 使用文档坐标计算偏移

            // 检查当前选中的是否是箭头
            if (!dynamic_cast<ShapeArrow *>(shapes[selectedIndex].get()))
            {
                // 如果不是箭头，直接移动并更新连接
                shapes[selectedIndex]->moveBy(delta);
                // 保证updateConnectedArrows在合法范围内调用
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(shapes.size()))
                {
                    updateConnectedArrows(selectedIndex, delta);
                }
            }

            lastMousePos = docPos; // 无论是否移动都更新鼠标位置
            update();
        }
    }
}

void DrawingArea::mouseReleaseEvent(QMouseEvent *event)
{
    // 转换屏幕坐标到文档坐标
    QPoint docPos = screenToDoc(event->pos());

    if (selectedIndex != -1)
    {
        if (auto *arrow = dynamic_cast<ShapeArrow *>(shapes[selectedIndex].get()))
        {
            // 处理箭头终点的连接情况
            if (arrow->getSelectedHandleIndex() == 1) // 如果是终点被选中
            {
                // 查找是否有目标图形的箭头锚点可以连接
                int bestShapeIndex = -1;
                int bestHandleIndex = -1;
                QPoint bestPoint;
                const int snapDistance = 10 / m_zoomFactor; // 缩放调整吸附距离

                // 检查所有图形的锚点
                for (size_t i = 0; i < shapes.size(); ++i)
                {
                    if (i == selectedIndex)
                        continue;
                    if (dynamic_cast<ShapeArrow *>(shapes[i].get()))
                        continue;

                    const auto &arrowAnchors = shapes[i]->getArrowAnchors();
                    for (size_t j = 0; j < arrowAnchors.size(); ++j)
                    {
                        if (arrowAnchors[j].type != ShapeBase::Handle::ArrowAnchor)
                            continue;

                        QPoint target = arrowAnchors[j].rect.center();
                        // 排除已经连接到的起点
                        QPoint startPoint = arrow->getLine().p1();
                        if (target == startPoint)
                            continue;

                        if ((docPos - target).manhattanLength() <= snapDistance)
                        {
                            bestShapeIndex = i;
                            bestHandleIndex = j;
                            bestPoint = target;
                            break;
                        }
                    }
                    if (bestShapeIndex != -1)
                        break;
                }

                // 找到可连接的目标
                // 找到可连接的目标
                if (bestShapeIndex != -1)
                {
                    // 设置箭头终点位置
                    arrow->setP2(bestPoint);

                    // 清除终点的已有连接
                    arrowConnections.erase(
                        std::remove_if(arrowConnections.begin(), arrowConnections.end(),
                                       [this](const ArrowConnection &conn)
                                       {
                                           return conn.arrowIndex == selectedIndex && !conn.isStartPoint;
                                       }),
                        arrowConnections.end());

                    // 添加新的终点连接
                    ArrowConnection connection;
                    connection.arrowIndex = selectedIndex;
                    connection.shapeIndex = bestShapeIndex;
                    connection.handleIndex = bestHandleIndex;
                    connection.isStartPoint = false;
                    arrowConnections.push_back(connection);

                    // 确保连接点正确
                    updateConnectedArrows(bestShapeIndex, QPoint());
                }
            }
            else if (arrow->getSelectedHandleIndex() == 0) // 如果是起点被选中
            {
                // 查找是否有目标图形的箭头锚点可以连接
                int bestShapeIndex = -1;
                int bestHandleIndex = -1;
                QPoint bestPoint;
                const int snapDistance = 10 / m_zoomFactor; // 缩放调整吸附距离

                // 检查所有图形的锚点
                for (size_t i = 0; i < shapes.size(); ++i)
                {
                    if (i == selectedIndex)
                        continue;
                    if (dynamic_cast<ShapeArrow *>(shapes[i].get()))
                        continue;

                    const auto &arrowAnchors = shapes[i]->getArrowAnchors();
                    for (size_t j = 0; j < arrowAnchors.size(); ++j)
                    {
                        if (arrowAnchors[j].type != ShapeBase::Handle::ArrowAnchor)
                            continue;

                        QPoint target = arrowAnchors[j].rect.center();
                        // 排除已经连接到的终点
                        QPoint endPoint = arrow->getLine().p2();
                        if (target == endPoint)
                            continue;

                        if ((docPos - target).manhattanLength() <= snapDistance)
                        {
                            bestShapeIndex = i;
                            bestHandleIndex = j;
                            bestPoint = target;
                            break;
                        }
                    }
                    if (bestShapeIndex != -1)
                        break;
                }

                // 找到可连接的目标
                if (bestShapeIndex != -1)
                {
                    // 设置箭头起点位置
                    arrow->setP1(bestPoint);

                    // 清除起点的已有连接
                    arrowConnections.erase(
                        std::remove_if(arrowConnections.begin(), arrowConnections.end(),
                                       [this](const ArrowConnection &conn)
                                       {
                                           return conn.arrowIndex == selectedIndex && conn.isStartPoint;
                                       }),
                        arrowConnections.end());

                    // 添加新的起点连接
                    ArrowConnection connection;
                    connection.arrowIndex = selectedIndex;
                    connection.shapeIndex = bestShapeIndex;
                    connection.handleIndex = bestHandleIndex;
                    connection.isStartPoint = true;
                    arrowConnections.push_back(connection);

                    // 确保连接点正确
                    updateConnectedArrows(bestShapeIndex, QPoint());
                }
            }

            // 清除箭头的选中状态
            arrow->clearHandleSelection();

            // 发送选中图形信号确保属性面板更新
            if (selectedIndex != -1)
            {
                emit shapeSelected(shapes[selectedIndex].get());
            }
        }
        else
        {
            // 如果拖拽结束，且是移动操作（非调整大小）
            if (dragging && !shapes[selectedIndex]->isHandleSelected())
            {
                // 计算从按下鼠标时到释放的总移动距离
                QPoint totalDelta = docPos - m_moveStartPos;

                // 只有当真实移动了一定距离时才记录移动操作
                if (totalDelta.manhattanLength() > 0)
                {
                    // 记录移动操作
                    recordMoveShape(selectedIndex, totalDelta);
                }
            }
            // 如果是调整大小操作结束
            else if (resizing && shapes[selectedIndex]->isHandleSelected())
            {
                QRect newRect = shapes[selectedIndex]->getRect();
                if (newRect != originalRect)
                {
                    // 记录调整大小操作
                    recordResizeShape(selectedIndex, originalRect, newRect);
                }
                resizing = false;
            }

            shapes[selectedIndex]->clearHandleSelection();

            // 释放鼠标后重新发送选中图形信号，确保属性面板更新
            emit shapeSelected(shapes[selectedIndex].get());
        }
    }
    snappedHandle = {-1, -1, QPoint()};
    dragging = false;
    update();
}

void DrawingArea::mouseDoubleClickEvent(QMouseEvent *event)
{
    // 转换屏幕坐标到文档坐标
    QPoint docPos = screenToDoc(event->pos());

    // 查找点击的图形
    for (int i = shapes.size() - 1; i >= 0; --i)
    {
        // 直接在文档坐标系中检查
        if (shapes[i]->contains(docPos))
        {
            // 检查图形是否支持文本编辑
            if (shapes[i]->isTextEditable())
            {
                startTextEditing(i);
                return;
            }
        }
    }
}

void DrawingArea::startTextEditing(int shapeIndex)
{
    // 如果已经在编辑其他图形，先完成编辑
    if (m_textEdit)
    {
        finishTextEditing();
    }

    // 根据图形类型创建不同的文本编辑控件
    if (dynamic_cast<ShapeEllipse *>(shapes[shapeIndex].get()))
    {
        m_textEdit = new EllipseTextEdit(this);
    }
    else
    {
        m_textEdit = new QLineEdit(this);
    }

    // 设置文本编辑控件的基本属性，考虑缩放因子
    QRect docRect = shapes[shapeIndex]->boundingRect().adjusted(5, 5, -5, -5);
    QRect screenRect = docToScreen(docRect);
    m_textEdit->setGeometry(screenRect);
    if (auto *lineEdit = qobject_cast<QLineEdit *>(m_textEdit))
    {
        lineEdit->setText(shapes[shapeIndex]->getText());
        lineEdit->setAlignment(Qt::AlignCenter);
        lineEdit->show();
        lineEdit->setFocus();

        // 连接编辑完成的信号
        connect(lineEdit, &QLineEdit::editingFinished, this, &DrawingArea::finishTextEditing);
        connect(lineEdit, &QLineEdit::returnPressed, this, &DrawingArea::finishTextEditing);
    }

    // 设置图形为编辑状态
    shapes[shapeIndex]->setEditing(true);
    selectedIndex = shapeIndex;
}

void DrawingArea::finishTextEditing()
{
    if (!m_textEdit || selectedIndex == -1)
        return;

    // 获取编辑后的文本
    QString newText;
    if (auto *lineEdit = qobject_cast<QLineEdit *>(m_textEdit))
    {
        newText = lineEdit->text();
    }
    shapes[selectedIndex]->setText(newText);
    shapes[selectedIndex]->setEditing(false);

    // 清理编辑控件
    m_textEdit->deleteLater();
    m_textEdit = nullptr;

    // 更新显示
    update();
}

void DrawingArea::keyPressEvent(QKeyEvent *event)
{
    if (m_textEdit && m_textEdit->hasFocus())
    {
        // 如果正在编辑文本，让文本编辑控件处理键盘事件
        QWidget::keyPressEvent(event);
        return;
    }

    // 处理撤销和重做快捷键
    if (event->modifiers() & Qt::ControlModifier)
    {
        if (event->key() == Qt::Key_Z)
        {
            undo();
            event->accept();
            return;
        }
        else if (event->key() == Qt::Key_Y)
        {
            redo();
            event->accept();
            return;
        }
    }

    if (selectedIndex != -1 &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace))
    {
        // 记录删除操作到历史
        recordRemoveShape(selectedIndex);

        // 删除相关的箭头连接
        arrowConnections.erase(
            std::remove_if(arrowConnections.begin(), arrowConnections.end(),
                           [this](const ArrowConnection &conn)
                           {
                               return conn.shapeIndex == selectedIndex ||
                                      conn.arrowIndex == selectedIndex;
                           }),
            arrowConnections.end());

        shapes.erase(shapes.begin() + selectedIndex);
        selectedIndex = -1;
        emit selectionCleared();
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void DrawingArea::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-shape-type"))
    {
        event->acceptProposedAction();
    }
}

void DrawingArea::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-shape-type"))
    {
        QByteArray shapeTypeData =
            event->mimeData()->data("application/x-shape-type");
        QString shapeType = QString::fromUtf8(shapeTypeData);
        QPoint pos = event->pos();
        std::unique_ptr<ShapeBase> shape;
        QRect defaultRect(pos.x() - 40, pos.y() - 30, 80, 60);

        // 暂时只处理矩形和椭圆
        if (shapeType == "rect")
        {
            shape = ShapeFactory::createRect(defaultRect);
        }
        else if (shapeType == "ellipse")
        {
            shape = ShapeFactory::createEllipse(defaultRect);
        }
        else if (shapeType == "pentagon")
        {
            shape = ShapeFactory::createPentagon(defaultRect);
        }
        else if (shapeType == "triangle")
        {
            shape = ShapeFactory::createTriangle(defaultRect);
        }
        else if (shapeType == "diamond")
        {
            shape = ShapeFactory::createDiamond(defaultRect);
        }
        else if (shapeType == "arrow")
        {
            QLine line(pos.x() - 40, pos.y(), pos.x() + 40, pos.y());
            shape = ShapeFactory::createArrow(line);
        }
        else if (shapeType == "roundedrect")
        {
            shape = ShapeFactory::createRoundedRect(defaultRect);
        }

        if (shape)
        {
            // 记录最初的索引
            int newIndex = shapes.size();

            // 添加到图形列表
            shapes.push_back(std::move(shape));
            selectedIndex = shapes.size() - 1;

            // 记录添加图形到历史
            recordAddShape(selectedIndex);

            update();
        }
        event->acceptProposedAction();
    }
}

void DrawingArea::updateConnectedArrows(int shapeIndex, const QPoint &delta)
{
    if (shapeIndex < 0 || shapeIndex >= shapes.size())
        return;

    // 获取当前图形的所有箭头锚点的最新位置
    const auto &arrowAnchors = shapes[shapeIndex]->getArrowAnchors();

    // 遍历所有箭头连接
    for (auto it = arrowConnections.begin(); it != arrowConnections.end(); ++it)
    {
        const auto &conn = *it;
        // 如果连接的是当前移动的图形
        if (conn.shapeIndex == shapeIndex)
        {
            // 找到对应的箭头
            if (conn.arrowIndex >= 0 && conn.arrowIndex < shapes.size())
            {
                // 检查是否是箭头类型
                auto *arrow = dynamic_cast<ShapeArrow *>(shapes[conn.arrowIndex].get());
                if (arrow)
                {
                    // 更新箭头连接点位置
                    if (conn.handleIndex >= 0 && conn.handleIndex < arrowAnchors.size())
                    {
                        // 使用图形上的锚点的实时位置，而不仅仅是添加delta
                        QPoint anchorPos = arrowAnchors[conn.handleIndex].rect.center();
                        if (conn.isStartPoint)
                        {
                            arrow->setP1(anchorPos);
                        }
                        else
                        {
                            arrow->setP2(anchorPos);
                        }
                    }
                    else
                    {
                        // 如果找不到锚点，就使用delta进行相对移动
                        arrow->updateConnection(conn.isStartPoint, delta);
                    }
                }
            }
        }
        // 处理该图形就是箭头自身的情况
        if (conn.arrowIndex == shapeIndex)
        {
            continue; // 箭头自身移动不需要处理
        }
    }

    // 第二遍检查：查找未记录的连接
    for (size_t i = 0; i < shapes.size(); ++i)
    {
        auto *arrow = dynamic_cast<ShapeArrow *>(shapes[i].get());
        if (!arrow || i == shapeIndex)
            continue;

        // 获取箭头的端点
        QPoint p1 = arrow->getLine().p1();
        QPoint p2 = arrow->getLine().p2();

        // 获取当前移动图形的箭头锚点
        const auto &anchors = shapes[shapeIndex]->getArrowAnchors();
        const int snapDistance = 5;

        // 检查是否已有连接记录
        bool startPointConnected = false;
        bool endPointConnected = false;

        for (const auto &conn : arrowConnections)
        {
            if (conn.arrowIndex == i && conn.shapeIndex == shapeIndex)
            {
                if (conn.isStartPoint)
                    startPointConnected = true;
                else
                    endPointConnected = true;

                // 更新已有连接的位置
                QPoint newAnchorPos = anchors[conn.handleIndex].rect.center();
                if (conn.isStartPoint)
                    arrow->setP1(newAnchorPos);
                else
                    arrow->setP2(newAnchorPos);
            }
        }

        // 检查未记录的起点连接
        if (!startPointConnected)
        {
            for (size_t j = 0; j < anchors.size(); ++j)
            {
                QPoint anchorPos = anchors[j].rect.center();
                if ((p1 - anchorPos).manhattanLength() <= snapDistance)
                {
                    ArrowConnection newConn;
                    newConn.arrowIndex = i;
                    newConn.shapeIndex = shapeIndex;
                    newConn.handleIndex = j;
                    newConn.isStartPoint = true;
                    arrowConnections.push_back(newConn);
                    arrow->setP1(anchorPos);
                    break;
                }
            }
        }

        // 检查未记录的终点连接
        if (!endPointConnected)
        {
            for (size_t j = 0; j < anchors.size(); ++j)
            {
                QPoint anchorPos = anchors[j].rect.center();
                if ((p2 - anchorPos).manhattanLength() <= snapDistance)
                {
                    ArrowConnection newConn;
                    newConn.arrowIndex = i;
                    newConn.shapeIndex = shapeIndex;
                    newConn.handleIndex = j;
                    newConn.isStartPoint = false;
                    arrowConnections.push_back(newConn);
                    arrow->setP2(anchorPos);
                    break;
                }
            }
        }
    }
}

void DrawingArea::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    QAction *copyAction = m_contextMenu->addAction(tr("Copy"));
    QAction *cutAction = m_contextMenu->addAction(tr("Cut"));
    QAction *pasteAction = m_contextMenu->addAction(tr("Paste"));
    m_contextMenu->addSeparator();
    QAction *deleteAction = m_contextMenu->addAction(tr("Delete"));

    // 根据是否有选中图形来设置菜单项的可用状态
    copyAction->setEnabled(selectedIndex != -1);
    cutAction->setEnabled(selectedIndex != -1);
    deleteAction->setEnabled(selectedIndex != -1);
    pasteAction->setEnabled(m_clipboardShape != nullptr);

    connect(copyAction, &QAction::triggered, this,
            &DrawingArea::copySelectedShape);
    connect(cutAction, &QAction::triggered, this, &DrawingArea::cutSelectedShape);
    connect(pasteAction, &QAction::triggered, this, &DrawingArea::pasteShape);
    connect(deleteAction, &QAction::triggered, this,
            &DrawingArea::deleteSelectedShape);
}

void DrawingArea::contextMenuEvent(QContextMenuEvent *event)
{
    // 更新菜单项的可用状态
    if (m_contextMenu)
    {
        for (QAction *action : m_contextMenu->actions())
        {
            if (action->text() == tr("Copy") || action->text() == tr("Cut") ||
                action->text() == tr("Delete"))
            {
                action->setEnabled(selectedIndex != -1);
            }
            else if (action->text() == tr("Paste"))
            {
                action->setEnabled(m_clipboardShape != nullptr);
            }
        }
    }

    // 显示菜单
    m_contextMenu->exec(event->globalPos());
}

void DrawingArea::copySelectedShape()
{
    if (selectedIndex != -1 && selectedIndex < shapes.size())
    {
        m_clipboardShape = shapes[selectedIndex]->clone();
    }
}

void DrawingArea::cutSelectedShape()
{
    if (selectedIndex != -1 && selectedIndex < shapes.size())
    {
        copySelectedShape();
        deleteSelectedShape();
    }
}

void DrawingArea::pasteShape()
{
    if (m_clipboardShape)
    {
        std::unique_ptr<ShapeBase> newShape = m_clipboardShape->clone();
        // 将新图形放在鼠标当前位置
        QPoint pos = mapFromGlobal(QCursor::pos());
        QRect rect = newShape->getRect();
        int width = rect.width();
        int height = rect.height();
        rect.setTopLeft(pos);
        rect.setWidth(width);
        rect.setHeight(height);
        newShape->setRect(rect);

        // 添加到图形列表
        int newIndex = shapes.size();
        shapes.push_back(std::move(newShape));
        selectedIndex = shapes.size() - 1;

        // 记录添加操作
        recordAddShape(selectedIndex);

        update();
    }
}

void DrawingArea::deleteSelectedShape()
{
    if (selectedIndex != -1 && selectedIndex < shapes.size())
    {
        // 记录删除操作到历史
        recordRemoveShape(selectedIndex);

        // 删除相关的箭头连接
        auto it = std::remove_if(arrowConnections.begin(), arrowConnections.end(),
                                 [this](const ArrowConnection &conn)
                                 {
                                     return conn.arrowIndex == selectedIndex || conn.shapeIndex == selectedIndex;
                                 });
        arrowConnections.erase(it, arrowConnections.end());

        // 删除选中的图形
        shapes.erase(shapes.begin() + selectedIndex);
        selectedIndex = -1;
        emit selectionCleared();
        update();
    }
}

void DrawingArea::moveShapeUp()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(shapes.size()) - 1)
    {
        return;
    }

    // 交换当前图形和上一个图形
    std::swap(shapes[selectedIndex], shapes[selectedIndex + 1]);
    selectedIndex++;
    update();
}

void DrawingArea::moveShapeDown()
{
    if (selectedIndex <= 0 || selectedIndex >= static_cast<int>(shapes.size()))
    {
        return;
    }

    // 交换当前图形和下一个图形
    std::swap(shapes[selectedIndex], shapes[selectedIndex - 1]);
    selectedIndex--;
    update();
}

void DrawingArea::moveShapeToTop()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(shapes.size()))
    {
        return;
    }

    // 将选中的图形移到最顶层
    auto shape = std::move(shapes[selectedIndex]);
    shapes.erase(shapes.begin() + selectedIndex);
    shapes.push_back(std::move(shape));
    selectedIndex = shapes.size() - 1;
    update();
}

void DrawingArea::moveShapeToBottom()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(shapes.size()))
    {
        return;
    }

    // 将选中的图形移到最底层
    auto shape = std::move(shapes[selectedIndex]);
    shapes.erase(shapes.begin() + selectedIndex);
    shapes.insert(shapes.begin(), std::move(shape));
    selectedIndex = 0;
    update();
}

void DrawingArea::setSelectedShapeLineColor(const QColor &color)
{
    if (selectedIndex >= 0 && selectedIndex < shapes.size())
    {
        QColor oldColor = shapes[selectedIndex]->getLineColor();
        int oldWidth = shapes[selectedIndex]->getLineWidth();

        // 如果颜色没有变化，不需要记录
        if (oldColor == color)
            return;

        // 记录属性变更
        recordPropertyChange(selectedIndex, oldColor, color, oldWidth, oldWidth);

        shapes[selectedIndex]->setLineColor(color);
        update();
    }
}

void DrawingArea::setSelectedShapeLineWidth(int width)
{
    if (selectedIndex >= 0 && selectedIndex < shapes.size())
    {
        QColor oldColor = shapes[selectedIndex]->getLineColor();
        int oldWidth = shapes[selectedIndex]->getLineWidth();

        // 如果宽度没有变化，不需要记录
        if (oldWidth == width)
            return;

        // 记录属性变更
        recordPropertyChange(selectedIndex, oldColor, oldColor, oldWidth, width);

        shapes[selectedIndex]->setLineWidth(width);
        update();
    }
}

// 设置缩放因子
void DrawingArea::setZoomFactor(double factor)
{
    // 限制缩放范围，防止太小或太大
    if (factor < 0.1)
        factor = 0.1;
    else if (factor > 5.0)
        factor = 5.0;

    if (m_zoomFactor != factor)
    {
        m_zoomFactor = factor;

        // 调整窗口大小以适应缩放后的页面，添加一些边距
        QSize newSize = docToScreen(m_pageSize);
        // 添加边距，使缩放后的页面周围有一些空间
        int margin = 40; // 40像素的边距
        newSize.setWidth(newSize.width() + margin * 2);
        newSize.setHeight(newSize.height() + margin * 2);
        setMinimumSize(newSize);
        resize(newSize);

        // 需要更新内容
        update();

        // 如果有文本编辑框打开，需要调整其位置和大小
        if (m_textEdit && selectedIndex >= 0 && selectedIndex < static_cast<int>(shapes.size()))
        {
            QRect rect = docToScreen(shapes[selectedIndex]->getRect());
            m_textEdit->setGeometry(rect);
        }

        // 发出缩放因子变化信号
        emit zoomFactorChanged(m_zoomFactor);
    }
}

// 放大
void DrawingArea::zoomIn()
{
    setZoomFactor(m_zoomFactor * 1.2); // 每次放大20%
}

// 缩小
void DrawingArea::zoomOut()
{
    setZoomFactor(m_zoomFactor / 1.2); // 每次缩小约17%
}

// 重置缩放
void DrawingArea::resetZoom()
{
    setZoomFactor(1.0);
}

// 滚轮事件处理（支持Ctrl+滚轮缩放）
void DrawingArea::wheelEvent(QWheelEvent *event)
{
    // 检查是否按下了Ctrl键
    if (event->modifiers() & Qt::ControlModifier)
    {
        // 获取滚轮的垂直角度增量
        int delta = event->angleDelta().y();

        if (delta > 0)
        {
            // 向上滚动，放大
            zoomIn();
        }
        else if (delta < 0)
        {
            // 向下滚动，缩小
            zoomOut();
        }

        event->accept(); // 标记事件已处理
    }
    else
    {
        // 如果没有按Ctrl，则交给基类处理（可能是滚动操作）
        QWidget::wheelEvent(event);
    }
}

// 屏幕坐标转文档坐标
QPoint DrawingArea::screenToDoc(const QPoint &pos) const
{
    return QPoint(pos.x() / m_zoomFactor, pos.y() / m_zoomFactor);
}

// 文档坐标转屏幕坐标
QPoint DrawingArea::docToScreen(const QPoint &pos) const
{
    return QPoint(pos.x() * m_zoomFactor, pos.y() * m_zoomFactor);
}

// 屏幕矩形转文档矩形
QRect DrawingArea::screenToDoc(const QRect &rect) const
{
    return QRect(
        rect.x() / m_zoomFactor,
        rect.y() / m_zoomFactor,
        rect.width() / m_zoomFactor,
        rect.height() / m_zoomFactor);
}

// 文档矩形转屏幕矩形
QRect DrawingArea::docToScreen(const QRect &rect) const
{
    return QRect(
        rect.x() * m_zoomFactor,
        rect.y() * m_zoomFactor,
        rect.width() * m_zoomFactor,
        rect.height() * m_zoomFactor);
}

// 文档大小转屏幕大小
QSize DrawingArea::docToScreen(const QSize &size) const
{
    return QSize(
        size.width() * m_zoomFactor,
        size.height() * m_zoomFactor);
}

// 撤销操作
void DrawingArea::undo()
{
    if (m_undoStack.empty())
    {
        return;
    }

    // 设置标志避免再次记录本次操作
    m_ignoreHistoryActions = true;

    // 取出最近的操作
    HistoryAction action = std::move(m_undoStack.top());
    m_undoStack.pop();

    // 根据操作类型执行相反的操作
    switch (action.type)
    {
    case OperationType::Add:
        // 添加操作的撤销：删除图形
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到重做栈中，确保shape所有权正确转移
            HistoryAction redoAction(OperationType::Add, action.shapeIndex);
            redoAction.shape = shapes[action.shapeIndex]->clone();
            m_redoStack.push(std::move(redoAction));

            // 执行删除
            shapes.erase(shapes.begin() + action.shapeIndex);
            if (selectedIndex == action.shapeIndex)
            {
                selectedIndex = -1;
                emit selectionCleared();
            }
            else if (selectedIndex > action.shapeIndex)
            {
                selectedIndex--;
            }
            update();
        }
        break;

    case OperationType::Remove:
        // 删除操作的撤销：重新添加图形
        if (action.shape)
        {
            // 确保action.shape不为nullptr后再进行操作
            HistoryAction redoAction(OperationType::Remove, action.shapeIndex);
            redoAction.shape = action.shape->clone(); // 创建副本，避免所有权转移问题

            // 记录连接关系
            for (const auto &conn : action.connections)
            {
                redoAction.connections.push_back(conn);
            }

            // 保存到重做栈
            m_redoStack.push(std::move(redoAction));

            // 恢复箭头连接关系
            for (const auto &conn : action.connections)
            {
                arrowConnections.push_back(conn);
            }

            // 在原来的位置插入图形
            int insertIndex = std::min(action.shapeIndex, (int)shapes.size());
            shapes.insert(shapes.begin() + insertIndex, std::move(action.shape));

            // 更新选择索引
            if (selectedIndex >= insertIndex)
            {
                selectedIndex++;
            }
            update();
        }
        break;

    case OperationType::Move:
        // 移动操作的撤销：移回原位置
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 创建重做动作
            HistoryAction redoAction(OperationType::Move, action.shapeIndex);
            redoAction.moveDelta = action.moveDelta;

            // 保存到重做栈
            m_redoStack.push(std::move(redoAction));

            // 执行反向移动
            QPoint delta = -action.moveDelta; // 反向移动
            shapes[action.shapeIndex]->moveBy(delta);

            // 更新连接的箭头位置
            updateConnectedArrows(action.shapeIndex, delta);
            update();
        }
        break;

    case OperationType::Resize:
        // 调整尺寸操作的撤销：恢复原来的尺寸
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到重做栈
            m_redoStack.push(std::move(action));

            // 恢复原来的尺寸
            shapes[action.shapeIndex]->setRect(action.oldRect);
            update();
        }
        break;

    case OperationType::Property:
        // 属性更改操作的撤销：恢复原来的属性
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到重做栈
            m_redoStack.push(std::move(action));

            // 恢复原来的线条颜色和粗细
            shapes[action.shapeIndex]->setLineColor(action.oldLineColor);
            shapes[action.shapeIndex]->setLineWidth(action.oldLineWidth);
            update();
        }
        break;
    }

    // 恢复标志
    m_ignoreHistoryActions = false;

    // 发出信号通知状态变化
    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 重做操作
void DrawingArea::redo()
{
    if (m_redoStack.empty())
    {
        return;
    }

    // 设置标志避免再次记录本次操作
    m_ignoreHistoryActions = true;

    // 取出最近的操作
    HistoryAction action = std::move(m_redoStack.top());
    m_redoStack.pop();

    // 根据操作类型执行相应的操作
    switch (action.type)
    {
    case OperationType::Add:
        // 添加操作的重做：重新添加图形
        if (action.shape)
        {
            // 确保有一个副本可供撤销操作使用
            HistoryAction undoAction(OperationType::Add, action.shapeIndex);
            undoAction.shape = action.shape->clone();
            m_undoStack.push(std::move(undoAction));

            // 在原来的位置插入图形
            int insertIndex = std::min(action.shapeIndex, (int)shapes.size());
            shapes.insert(shapes.begin() + insertIndex, std::move(action.shape));

            // 更新选择索引
            if (selectedIndex >= insertIndex)
            {
                selectedIndex++;
            }
            update();
        }
        break;

    case OperationType::Remove:
        // 删除操作的重做：再次删除图形
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到撤销栈
            m_undoStack.push(std::move(action));

            // 执行删除
            shapes.erase(shapes.begin() + action.shapeIndex);
            if (selectedIndex == action.shapeIndex)
            {
                selectedIndex = -1;
                emit selectionCleared();
            }
            else if (selectedIndex > action.shapeIndex)
            {
                selectedIndex--;
            }
            update();
        }
        break;

    case OperationType::Move:
        // 移动操作的重做：重新移动
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到撤销栈
            m_undoStack.push(std::move(action));

            // 执行移动
            shapes[action.shapeIndex]->moveBy(action.moveDelta);

            // 更新连接的箭头位置
            updateConnectedArrows(action.shapeIndex, action.moveDelta);
            update();
        }
        break;

    case OperationType::Resize:
        // 调整尺寸操作的重做：再次调整尺寸
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到撤销栈
            m_undoStack.push(std::move(action));

            // 设置新的尺寸
            shapes[action.shapeIndex]->setRect(action.newRect);
            update();
        }
        break;

    case OperationType::Property:
        // 属性更改操作的重做：再次更改属性
        if (action.shapeIndex >= 0 && action.shapeIndex < shapes.size())
        {
            // 保存到撤销栈
            m_undoStack.push(std::move(action));

            // 设置新的线条颜色和粗细
            shapes[action.shapeIndex]->setLineColor(action.newLineColor);
            shapes[action.shapeIndex]->setLineWidth(action.newLineWidth);
            update();
        }
        break;
    }

    // 恢复标志
    m_ignoreHistoryActions = false;

    // 发出信号通知状态变化
    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 记录添加图形操作
void DrawingArea::recordAddShape(int index)
{
    if (m_ignoreHistoryActions)
        return;

    if (index < 0 || index >= shapes.size())
    {
        return;
    }

    HistoryAction action(OperationType::Add, index);

    // 为添加操作保存图形的副本，确保正确克隆
    action.shape = shapes[index]->clone();

    m_undoStack.push(std::move(action));
    clearRedoStack();

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 记录删除图形操作
void DrawingArea::recordRemoveShape(int index)
{
    if (m_ignoreHistoryActions || index < 0 || index >= shapes.size())
        return;

    HistoryAction action(OperationType::Remove, index);

    // 克隆当前图形
    action.shape = shapes[index]->clone();

    // 记录和该图形相关的箭头连接
    for (const auto &conn : arrowConnections)
    {
        if (conn.shapeIndex == index)
        {
            action.connections.push_back(conn);
        }
    }

    m_undoStack.push(std::move(action));
    clearRedoStack();

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 记录移动图形操作
void DrawingArea::recordMoveShape(int index, const QPoint &delta)
{
    if (m_ignoreHistoryActions || index < 0 || index >= shapes.size())
        return;

    HistoryAction action(OperationType::Move, index);
    action.moveDelta = delta;

    m_undoStack.push(std::move(action));
    clearRedoStack();

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 记录调整图形尺寸操作
void DrawingArea::recordResizeShape(int index, const QRect &oldRect, const QRect &newRect)
{
    if (m_ignoreHistoryActions || index < 0 || index >= shapes.size())
        return;

    HistoryAction action(OperationType::Resize, index);
    action.oldRect = oldRect;
    action.newRect = newRect;

    m_undoStack.push(std::move(action));
    clearRedoStack();

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 记录图形属性变更操作
void DrawingArea::recordPropertyChange(int index, const QColor &oldColor, const QColor &newColor,
                                       int oldWidth, int newWidth)
{
    if (m_ignoreHistoryActions || index < 0 || index >= shapes.size())
        return;

    HistoryAction action(OperationType::Property, index);
    action.oldLineColor = oldColor;
    action.newLineColor = newColor;
    action.oldLineWidth = oldWidth;
    action.newLineWidth = newWidth;

    m_undoStack.push(std::move(action));
    clearRedoStack();

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
}

// 清空重做堆栈
void DrawingArea::clearRedoStack()
{
    while (!m_redoStack.empty())
    {
        m_redoStack.pop();
    }
    emit canRedoChanged(false);
}
