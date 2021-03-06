#include "vtextedit.h"

#include <QDebug>
#include <QScrollBar>
#include <QPainter>
#include <QResizeEvent>

#include "vtextdocumentlayout.h"
#include "vimageresourcemanager2.h"

#define VIRTUAL_CURSOR_BLOCK_WIDTH 8

enum class BlockState
{
    Normal = 0,
    CodeBlockStart,
    CodeBlock,
    CodeBlockEnd,
    Comment
};


VTextEdit::VTextEdit(QWidget *p_parent)
    : QTextEdit(p_parent),
      m_imageMgr(nullptr)
{
    init();
}

VTextEdit::VTextEdit(const QString &p_text, QWidget *p_parent)
    : QTextEdit(p_text, p_parent),
      m_imageMgr(nullptr)
{
    init();
}

VTextEdit::~VTextEdit()
{
    if (m_imageMgr) {
        delete m_imageMgr;
    }
}

void VTextEdit::init()
{
    setAcceptRichText(false);

    m_lineNumberType = LineNumberType::None;

    m_blockImageEnabled = false;

    m_cursorBlockMode = CursorBlock::None;

    m_highlightCursorLineBlock = false;

    m_imageMgr = new VImageResourceManager2();

    QTextDocument *doc = document();
    VTextDocumentLayout *docLayout = new VTextDocumentLayout(doc, m_imageMgr);
    docLayout->setBlockImageEnabled(m_blockImageEnabled);
    doc->setDocumentLayout(docLayout);

    docLayout->setVirtualCursorBlockWidth(VIRTUAL_CURSOR_BLOCK_WIDTH);

    connect(docLayout, &VTextDocumentLayout::cursorBlockWidthUpdated,
            this, [this](int p_width) {
                if (p_width != cursorWidth()
                    && p_width > VIRTUAL_CURSOR_BLOCK_WIDTH) {
                    setCursorWidth(p_width);
                }
            });

    m_lineNumberArea = new VLineNumberArea(this,
                                           document(),
                                           fontMetrics().width(QLatin1Char('8')),
                                           fontMetrics().height(),
                                           this);
    connect(doc, &QTextDocument::blockCountChanged,
            this, &VTextEdit::updateLineNumberAreaMargin);
    connect(this, &QTextEdit::textChanged,
            this, &VTextEdit::updateLineNumberArea);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &VTextEdit::updateLineNumberArea);
    connect(this, &QTextEdit::cursorPositionChanged,
            this, [this]() {
                if (m_highlightCursorLineBlock) {
                    QTextCursor cursor = textCursor();
                    getLayout()->setCursorLineBlockNumber(cursor.block().blockNumber());
                }

                updateLineNumberArea();
            });
}

VTextDocumentLayout *VTextEdit::getLayout() const
{
    return qobject_cast<VTextDocumentLayout *>(document()->documentLayout());
}

void VTextEdit::setLineLeading(qreal p_leading)
{
    getLayout()->setLineLeading(p_leading);
}

void VTextEdit::resizeEvent(QResizeEvent *p_event)
{
    QTextEdit::resizeEvent(p_event);

    if (m_lineNumberType != LineNumberType::None) {
        QRect rect = contentsRect();
        m_lineNumberArea->setGeometry(QRect(rect.left(),
                                            rect.top(),
                                            m_lineNumberArea->calculateWidth(),
                                            rect.height()));
    }
}

void VTextEdit::paintLineNumberArea(QPaintEvent *p_event)
{
    if (m_lineNumberType == LineNumberType::None) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
        return;
    }

    QPainter painter(m_lineNumberArea);
    painter.fillRect(p_event->rect(), m_lineNumberArea->getBackgroundColor());

    QTextBlock block = firstVisibleBlock();
    if (!block.isValid()) {
        return;
    }

    VTextDocumentLayout *layout = getLayout();
    Q_ASSERT(layout);

    int blockNumber = block.blockNumber();
    QRectF rect = layout->blockBoundingRect(block);
    int top = contentOffsetY() + (int)rect.y();
    int bottom = top + (int)rect.height();
    int eventTop = p_event->rect().top();
    int eventBtm = p_event->rect().bottom();
    const int digitHeight = m_lineNumberArea->getDigitHeight();
    const int curBlockNumber = textCursor().block().blockNumber();
    painter.setPen(m_lineNumberArea->getForegroundColor());
    const int leading = (int)layout->getLineLeading();

    // Display line number only in code block.
    if (m_lineNumberType == LineNumberType::CodeBlock) {
        int number = 0;
        while (block.isValid() && top <= eventBtm) {
            int blockState = block.userState();
            switch (blockState) {
            case (int)BlockState::CodeBlockStart:
                Q_ASSERT(number == 0);
                number = 1;
                break;

            case (int)BlockState::CodeBlockEnd:
                number = 0;
                break;

            case (int)BlockState::CodeBlock:
                if (number == 0) {
                    // Need to find current line number in code block.
                    QTextBlock startBlock = block.previous();
                    while (startBlock.isValid()) {
                        if (startBlock.userState() == (int)BlockState::CodeBlockStart) {
                            number = block.blockNumber() - startBlock.blockNumber();
                            break;
                        }

                        startBlock = startBlock.previous();
                    }
                }

                break;

            default:
                break;
            }

            if (blockState == (int)BlockState::CodeBlock) {
                if (block.isVisible() && bottom >= eventTop) {
                    QString numberStr = QString::number(number);
                    painter.drawText(0,
                                     top + leading,
                                     m_lineNumberArea->width(),
                                     digitHeight,
                                     Qt::AlignRight,
                                     numberStr);
                }

                ++number;
            }

            block = block.next();
            top = bottom;
            bottom = top + (int)layout->blockBoundingRect(block).height();
        }

        return;
    }

    // Handle m_lineNumberType 1 and 2.
    Q_ASSERT(m_lineNumberType == LineNumberType::Absolute
             || m_lineNumberType == LineNumberType::Relative);
    while (block.isValid() && top <= eventBtm) {
        if (block.isVisible() && bottom >= eventTop) {
            bool currentLine = false;
            int number = blockNumber + 1;
            if (m_lineNumberType == LineNumberType::Relative) {
                number = blockNumber - curBlockNumber;
                if (number == 0) {
                    currentLine = true;
                    number = blockNumber + 1;
                } else if (number < 0) {
                    number = -number;
                }
            } else if (blockNumber == curBlockNumber) {
                currentLine = true;
            }

            QString numberStr = QString::number(number);

            if (currentLine) {
                QFont font = painter.font();
                font.setBold(true);
                painter.setFont(font);
            }

            painter.drawText(0,
                             top + leading,
                             m_lineNumberArea->width(),
                             digitHeight,
                             Qt::AlignRight,
                             numberStr);

            if (currentLine) {
                QFont font = painter.font();
                font.setBold(false);
                painter.setFont(font);
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + (int)layout->blockBoundingRect(block).height();
        ++blockNumber;
    }
}

void VTextEdit::updateLineNumberAreaMargin()
{
    int width = 0;
    if (m_lineNumberType != LineNumberType::None) {
        width = m_lineNumberArea->calculateWidth();
    }

    if (width != viewportMargins().left()) {
        setViewportMargins(width, 0, 0, 0);
    }
}

void VTextEdit::updateLineNumberArea()
{
    if (m_lineNumberType != LineNumberType::None) {
        if (!m_lineNumberArea->isVisible()) {
            updateLineNumberAreaMargin();
            m_lineNumberArea->show();
        }

        m_lineNumberArea->update();
    } else if (m_lineNumberArea->isVisible()) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
    }
}

QTextBlock VTextEdit::firstVisibleBlock() const
{
    VTextDocumentLayout *layout = getLayout();
    Q_ASSERT(layout);
    int blockNumber = layout->findBlockByPosition(QPointF(0, -contentOffsetY()));
    return document()->findBlockByNumber(blockNumber);
}

int VTextEdit::contentOffsetY() const
{
    QScrollBar *sb = verticalScrollBar();
    return -(sb->value());
}

void VTextEdit::clearBlockImages()
{
    m_imageMgr->clear();

    getLayout()->relayout();
}

void VTextEdit::relayout(const QSet<int> &p_blocks)
{
    getLayout()->relayout(p_blocks);
}

bool VTextEdit::containsImage(const QString &p_imageName) const
{
    return m_imageMgr->contains(p_imageName);
}

QSize VTextEdit::imageSize(const QString &p_imageName) const
{
    const QPixmap *img = m_imageMgr->findImage(p_imageName);
    if (img) {
        return img->size();
    }

    return QSize();
}

void VTextEdit::addImage(const QString &p_imageName, const QPixmap &p_image)
{
    if (m_blockImageEnabled) {
        m_imageMgr->addImage(p_imageName, p_image);
    }
}

void VTextEdit::removeImage(const QString &p_imageName)
{
    m_imageMgr->removeImage(p_imageName);
}

void VTextEdit::setBlockImageEnabled(bool p_enabled)
{
    if (m_blockImageEnabled == p_enabled) {
        return;
    }

    m_blockImageEnabled = p_enabled;

    getLayout()->setBlockImageEnabled(m_blockImageEnabled);

    if (!m_blockImageEnabled) {
        clearBlockImages();
    }
}

void VTextEdit::setImageWidthConstrainted(bool p_enabled)
{
    getLayout()->setImageWidthConstrainted(p_enabled);
}

void VTextEdit::setImageLineColor(const QColor &p_color)
{
    getLayout()->setImageLineColor(p_color);
}

void VTextEdit::setCursorBlockMode(CursorBlock p_mode)
{
    if (p_mode != m_cursorBlockMode) {
        m_cursorBlockMode = p_mode;
        getLayout()->setCursorBlockMode(m_cursorBlockMode);
        getLayout()->clearLastCursorBlockWidth();
        setCursorWidth(m_cursorBlockMode != CursorBlock::None ? VIRTUAL_CURSOR_BLOCK_WIDTH
                                                              : 1);
    }
}

void VTextEdit::setHighlightCursorLineBlockEnabled(bool p_enabled)
{
    if (m_highlightCursorLineBlock != p_enabled) {
        auto layout = getLayout();
        m_highlightCursorLineBlock = p_enabled;
        layout->setHighlightCursorLineBlockEnabled(p_enabled);
        if (m_highlightCursorLineBlock) {
            QTextCursor cursor = textCursor();
            layout->setCursorLineBlockNumber(cursor.block().blockNumber());
        }
    }
}

void VTextEdit::setCursorLineBlockBg(const QColor &p_bg)
{
    getLayout()->setCursorLineBlockBg(p_bg);
}

void VTextEdit::relayout()
{
    getLayout()->relayout();
}
