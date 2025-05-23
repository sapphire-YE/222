#include "ShapeBase.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void ShapeBase::paint(QPainter *painter, bool selected)
{
  if (!painter)
    return;

  // 保存当前变换状态
  painter->save();

  // 设置不透明度
  painter->setOpacity(m_opacity);

  // 设置旋转中心点和旋转角度
  QRect rect = boundingRect();
  QPoint center = rect.center();
  painter->translate(center);
  painter->rotate(m_rotation * 180.0 / M_PI); // 转换为角度
  painter->translate(-center);

  // 1. 先绘制图形本身
  paintShape(painter);

  // 2. 如果图形有文本，绘制文本
  if (!m_text.isEmpty())
  {
    painter->setPen(QPen(m_textColor.isValid() ? m_textColor : Qt::black)); // 使用文本颜色
    painter->setBrush(Qt::NoBrush);                                         // 文本不需要填充

    // 设置字体
    if (m_font.family() != "")
    {
      painter->setFont(m_font);
    }

    QRect textRect = boundingRect().adjusted(5, 5, -5, -5); // 留出边距
    painter->drawText(textRect, m_textAlignment, m_text);
  }

  // 恢复变换状态
  painter->restore();

  // 3. 如果被选中，绘制选中状态
  if (selected)
  {
    // 绘制虚线框，考虑旋转
    QRect rect = boundingRect();
    QPoint center = rect.center();
    
    // 保存绘图状态
    painter->save();
    
    // 设置旋转
    painter->translate(center);
    painter->rotate(m_rotation * 180.0 / M_PI); // 转换为角度
    painter->translate(-center);
    
    // 绘制旋转后的虚线框
    painter->setPen(QPen(Qt::blue, 1, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect);
    
    // 恢复绘图状态
    painter->restore();

    // 绘制所有锚点
    for (const auto &handle : getHandles())
    {
      if (handle.type == Handle::Scale)
      {
        // 绘制缩放锚点（白色填充的蓝色边框方块）
        painter->setBrush(Qt::white);
        painter->setPen(QPen(Qt::blue));
        painter->drawRect(handle.rect);
      }
      else if (handle.type == Handle::Arrow)
      {
        // 绘制箭头锚点（灰色加号）
        painter->setPen(QPen(Qt::gray, 2));
        painter->setBrush(Qt::NoBrush);
        QPoint center = handle.rect.center();
        painter->drawLine(center.x() - 5, center.y(), center.x() + 5,
                          center.y());
        painter->drawLine(center.x(), center.y() - 5, center.x(),
                          center.y() + 5);
      }
      else if (handle.type == Handle::Rotate)
      {
        // 绘制旋转锚点（圆形）
        painter->setPen(QPen(Qt::blue));
        painter->setBrush(Qt::white);
        painter->drawEllipse(handle.rect);
        // 绘制旋转指示线
        QPoint center = boundingRect().center();
        QPoint handleCenter = handle.rect.center();
        painter->drawLine(center, handleCenter);
      }
    }
  }
}

bool ShapeBase::handleAnchorInteraction(const QPoint &mousePos,
                                        const QPoint &lastMousePos)
{
  if (m_selectedHandleIndex == -1)
    return false;

  const auto &handles = getHandles();
  if (m_selectedHandleIndex >= handles.size())
    return false;

  const auto &handle = handles[m_selectedHandleIndex];

  if (handle.type == Handle::Rotate)
  {
    // 计算旋转角度
    QPoint center = boundingRect().center();
    QPoint lastVector = lastMousePos - center;
    QPoint currentVector = mousePos - center;

    double lastAngle = atan2(lastVector.y(), lastVector.x());
    double currentAngle = atan2(currentVector.y(), currentVector.x());
    double deltaAngle = currentAngle - lastAngle;

    // 更新旋转角度
    m_rotation += deltaAngle;
    rotate(deltaAngle);
    return true;
  }

  // 计算新的矩形区域
  QRect newRect = calculateNewRect(mousePos, lastMousePos);
  if (newRect.isEmpty())
    return false;

  // 调用子类的resize方法
  resize(newRect);
  return true;
}

QRect ShapeBase::calculateNewRect(const QPoint &mousePos,
                                  const QPoint &lastMousePos) const
{
  QRect currentRect = boundingRect();
  QPoint delta = mousePos - lastMousePos;
  QRect newRect = currentRect;

  // 根据选中的锚点类型和位置计算新的矩形
  switch (m_selectedHandleIndex)
  {
  case 0: // 左上
    newRect.setTopLeft(newRect.topLeft() + delta);
    break;
  case 1: // 上中
    newRect.setTop(newRect.top() + delta.y());
    break;
  case 2: // 右上
    newRect.setTopRight(newRect.topRight() + delta);
    break;
  case 3: // 左中
    newRect.setLeft(newRect.left() + delta.x());
    break;
  case 4: // 右中
    newRect.setRight(newRect.right() + delta.x());
    break;
  case 5: // 左下
    newRect.setBottomLeft(newRect.bottomLeft() + delta);
    break;
  case 6: // 下中
    newRect.setBottom(newRect.bottom() + delta.y());
    break;
  case 7: // 右下
    newRect.setBottomRight(newRect.bottomRight() + delta);
    break;
  default:
    return QRect();
  }

  // 确保矩形不会翻转
  if (newRect.width() < 1 || newRect.height() < 1)
  {
    return currentRect;
  }

  return newRect;
}

std::vector<ShapeBase::Handle> ShapeBase::getHandles() const
{
  std::vector<Handle> handles;
  QRect rect = boundingRect();
  int w = rect.width(), h = rect.height();
  int x = rect.left(), y = rect.top();
  int size = 8; // 锚点大小
  QPoint center = rect.center();

  // 如果图形有旋转，则需要计算旋转后的锚点位置
  if (m_rotation != 0.0) {
    // 计算旋转后的锚点位置
    auto rotatePoint = [&](QPoint pt) {
      QPoint localPt = pt - center;
      double cosAngle = cos(m_rotation);
      double sinAngle = sin(m_rotation);
      QPoint rotatedPt(
        localPt.x() * cosAngle - localPt.y() * sinAngle,
        localPt.x() * sinAngle + localPt.y() * cosAngle
      );
      return rotatedPt + center;
    };

    // 计算各个锚点的位置
    QPoint topLeft = rotatePoint(QPoint(x, y));
    QPoint topMiddle = rotatePoint(QPoint(x + w / 2, y));
    QPoint topRight = rotatePoint(QPoint(x + w, y));
    QPoint leftMiddle = rotatePoint(QPoint(x, y + h / 2));
    QPoint rightMiddle = rotatePoint(QPoint(x + w, y + h / 2));
    QPoint bottomLeft = rotatePoint(QPoint(x, y + h));
    QPoint bottomMiddle = rotatePoint(QPoint(x + w / 2, y + h));
    QPoint bottomRight = rotatePoint(QPoint(x + w, y + h));

    // 添加旋转后的锚点
    handles.push_back({QRect(topLeft.x() - 4, topLeft.y() - 4, size, size), Handle::Scale, 0}); // 左上
    handles.push_back({QRect(topMiddle.x() - 4, topMiddle.y() - 4, size, size), Handle::Scale, 1}); // 上中
    handles.push_back({QRect(topRight.x() - 4, topRight.y() - 4, size, size), Handle::Scale, 2}); // 右上
    handles.push_back({QRect(leftMiddle.x() - 4, leftMiddle.y() - 4, size, size), Handle::Scale, 3}); // 左中
    handles.push_back({QRect(rightMiddle.x() - 4, rightMiddle.y() - 4, size, size), Handle::Scale, 4}); // 右中
    handles.push_back({QRect(bottomLeft.x() - 4, bottomLeft.y() - 4, size, size), Handle::Scale, 5}); // 左下
    handles.push_back({QRect(bottomMiddle.x() - 4, bottomMiddle.y() - 4, size, size), Handle::Scale, 6}); // 下中
    handles.push_back({QRect(bottomRight.x() - 4, bottomRight.y() - 4, size, size), Handle::Scale, 7}); // 右下
  } else {
    // 无旋转时的原始实现
    handles.push_back({QRect(x - 4, y - 4, size, size), Handle::Scale, 0}); // 左上
    handles.push_back({QRect(x + w / 2 - 4, y - 4, size, size), Handle::Scale, 1}); // 上中
    handles.push_back({QRect(x + w - 4, y - 4, size, size), Handle::Scale, 2}); // 右上
    handles.push_back({QRect(x - 4, y + h / 2 - 4, size, size), Handle::Scale, 3}); // 左中
    handles.push_back({QRect(x + w - 4, y + h / 2 - 4, size, size), Handle::Scale, 4}); // 右中
    handles.push_back({QRect(x - 4, y + h - 4, size, size), Handle::Scale, 5}); // 左下
    handles.push_back({QRect(x + w / 2 - 4, y + h - 4, size, size), Handle::Scale, 6}); // 下中
    handles.push_back({QRect(x + w - 4, y + h - 4, size, size), Handle::Scale, 7}); // 右下
  }

  // 添加旋转锚点 - 旋转锚点始终在上方
  int rotateSize = 12;   // 旋转锚点稍大一些
  int rotateOffset = 30; // 距离边界的距离

  if (m_rotation != 0.0) {
    // 计算旋转后的旋转锚点位置
    QPoint rotateCenter = center;
    QPoint rotateHandlePos(center.x(), center.y() - h/2 - rotateOffset);
    
    // 将旋转锚点相对于中心旋转
    QPoint localPt = rotateHandlePos - center;
    double cosAngle = cos(m_rotation);
    double sinAngle = sin(m_rotation);
    QPoint rotatedPt(
      localPt.x() * cosAngle - localPt.y() * sinAngle,
      localPt.x() * sinAngle + localPt.y() * cosAngle
    );
    QPoint finalRotatePos = rotatedPt + center;
    
    handles.push_back({QRect(finalRotatePos.x() - rotateSize / 2, 
                             finalRotatePos.y() - rotateSize / 2,
                             rotateSize, rotateSize),
                      Handle::Rotate, 8});
  } else {
    handles.push_back(
      {QRect(x + w / 2 - rotateSize / 2, y - rotateOffset - rotateSize / 2,
             rotateSize, rotateSize),
       Handle::Rotate, 8});
  }

  // 只在需要时添加加号锚点
  if (needPlusHandles())
  {
    int arrowSize = 24; // 加号锚点区域大小
    int offset = 30;    // 距离边界的距离（更大）
    
    if (m_rotation != 0.0) {
      // 计算旋转后的加号锚点位置
      auto rotatePoint = [&](QPoint pt) {
        QPoint localPt = pt - center;
        double cosAngle = cos(m_rotation);
        double sinAngle = sin(m_rotation);
        QPoint rotatedPt(
          localPt.x() * cosAngle - localPt.y() * sinAngle,
          localPt.x() * sinAngle + localPt.y() * cosAngle
        );
        return rotatedPt + center;
      };
      
      // 计算各个加号锚点的旋转后位置
      QPoint topArrow = rotatePoint(QPoint(x + w / 2, y - offset + arrowSize/2));
      QPoint bottomArrow = rotatePoint(QPoint(x + w / 2, y + h + offset - arrowSize/2));
      QPoint leftArrow = rotatePoint(QPoint(x - offset + arrowSize/2, y + h / 2));
      QPoint rightArrow = rotatePoint(QPoint(x + w + offset - arrowSize/2, y + h / 2));
      
      // 添加旋转后的加号锚点
      handles.push_back({QRect(topArrow.x() - arrowSize/2, topArrow.y() - arrowSize/2, 
                              arrowSize, arrowSize), Handle::Arrow, 9}); // 上
      handles.push_back({QRect(bottomArrow.x() - arrowSize/2, bottomArrow.y() - arrowSize/2, 
                              arrowSize, arrowSize), Handle::Arrow, 10}); // 下
      handles.push_back({QRect(leftArrow.x() - arrowSize/2, leftArrow.y() - arrowSize/2, 
                              arrowSize, arrowSize), Handle::Arrow, 11}); // 左
      handles.push_back({QRect(rightArrow.x() - arrowSize/2, rightArrow.y() - arrowSize/2, 
                              arrowSize, arrowSize), Handle::Arrow, 12}); // 右
    } else {
      // 无旋转时的原始实现
      // 上
      handles.push_back(
          {QRect(x + w / 2 - arrowSize / 2, y - offset, arrowSize, arrowSize),
           Handle::Arrow, 9});
      // 下
      handles.push_back({QRect(x + w / 2 - arrowSize / 2,
                               y + h + offset - arrowSize, arrowSize, arrowSize),
                         Handle::Arrow, 10});
      // 左
      handles.push_back(
          {QRect(x - offset, y + h / 2 - arrowSize / 2, arrowSize, arrowSize),
           Handle::Arrow, 11});
      // 右
      handles.push_back({QRect(x + w + offset - arrowSize,
                               y + h / 2 - arrowSize / 2, arrowSize, arrowSize),
                         Handle::Arrow, 12});
    }
  }
  return handles;
}