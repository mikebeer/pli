// Builders that reproduce the look of the VM/370+ guide:
// Times New Roman 10.5pt body, Arial bold headings, grey tables, framed code figures.
const fs = require('fs');
const {
  Paragraph, TextRun, Table, TableRow, TableCell, WidthType, ShadingType, BorderStyle,
  AlignmentType, TabStopType, LeaderType, ImageRun, TableLayoutType, VerticalAlign,
} = require('docx');

const TNR = 'Times New Roman', ARIAL = 'Arial', COURIER = 'Courier New';
const W = 9638; // text width in DXA (A4, 2 cm margins)

// figure numbering: keys are resolved before rendering
const figNumbers = {};
function assignFigures(blocks) {
  let n = 0;
  for (const b of blocks) {
    if (b.fig) { n += 1; figNumbers[b.fig] = n; }
  }
}
function resolveRefs(s) {
  return s.replace(/\{fig:([a-z0-9_]+)\}/g, (m, k) => {
    if (!figNumbers[k]) throw new Error('unknown figure key ' + k);
    return 'Figure ' + figNumbers[k];
  });
}

// inline markup: `code`, **bold**, ~italic~
function rich(text, o = {}) {
  const size = o.size || 21;
  const codeSize = o.codeSize || 19;
  const font = o.font || TNR;
  const t = resolveRefs(text);
  const parts = t.split(/(`[^`]+`|\*\*[^*]+\*\*|~[^~]+~)/).filter((x) => x !== '');
  return parts.map((p) => {
    if (p.startsWith('`')) return new TextRun({ text: p.slice(1, -1), font: COURIER, size: codeSize });
    if (p.startsWith('**')) return new TextRun({ text: p.slice(2, -2), font, size, bold: true });
    if (p.startsWith('~')) return new TextRun({ text: p.slice(1, -1), font, size, italics: true });
    return new TextRun({ text: p, font, size, bold: !!o.bold });
  });
}

const para = (text, o = {}) => new Paragraph({
  children: rich(text, o),
  spacing: { after: o.after === undefined ? 120 : o.after, before: o.before || 0 },
  keepNext: !!o.keepNext,
});

const arial = (text, size, bold, extra = {}) => new Paragraph({
  children: [new TextRun({ text, font: ARIAL, size, bold })],
  ...extra,
});

function h1(text, o = {}) {
  return new Paragraph({ heading: 'Heading1', pageBreakBefore: !o.first, children: [new TextRun(text)] });
}
const h2 = (text) => new Paragraph({ heading: 'Heading2', children: [new TextRun(text)] });
const h3 = (text) => new Paragraph({ heading: 'Heading3', children: [new TextRun(text)] });

const bullets = (items) => items.map((it, i) => new Paragraph({
  numbering: { reference: 'bul', level: 0 },
  children: rich(it),
  spacing: { after: i === items.length - 1 ? 140 : 60 },
}));

let olInstance = 0;
const steps = (items) => {
  olInstance += 1;
  return items.map((it, i) => new Paragraph({
    numbering: { reference: 'num', level: 0, instance: olInstance },
    children: rich(it),
    spacing: { after: i === items.length - 1 ? 140 : 60 },
  }));
};

const caption = (key, text) => new Paragraph({
  spacing: { before: 80, after: 200 },
  children: [
    new TextRun({ text: `Figure ${figNumbers[key]}. `, font: ARIAL, size: 18, bold: true }),
    new TextRun({ text: resolveRefs(text), font: ARIAL, size: 18 }),
  ],
});

const none = { style: BorderStyle.NONE, size: 0, color: 'FFFFFF' };
const line = (c) => ({ style: BorderStyle.SINGLE, size: 4, color: c });

function codeFrame(lines, key, cap) {
  const paras = lines.map((l) => new Paragraph({
    spacing: { after: 0, line: 228 },
    keepNext: true,
    children: [new TextRun({ text: l === '' ? ' ' : l, font: COURIER, size: 17 })],
  }));
  const b = line('000000');
  const t = new Table({
    width: { size: W, type: WidthType.DXA },
    columnWidths: [W],
    layout: TableLayoutType.FIXED,
    borders: { top: b, bottom: b, left: b, right: b, insideHorizontal: b, insideVertical: b },
    rows: [new TableRow({
      cantSplit: true,
      children: [new TableCell({
        width: { size: W, type: WidthType.DXA },
        shading: { type: ShadingType.CLEAR, fill: 'F2F2F2', color: 'auto' },
        margins: { top: 100, bottom: 100, left: 140, right: 140 },
        children: paras,
      })],
    })],
  });
  return [t, caption(key, cap)];
}

function imageFrame(file, w, h, key, cap, alt) {
  const b = line('000000');
  const t = new Table({
    width: { size: W, type: WidthType.DXA },
    columnWidths: [W],
    layout: TableLayoutType.FIXED,
    borders: { top: b, bottom: b, left: b, right: b, insideHorizontal: b, insideVertical: b },
    rows: [new TableRow({
      cantSplit: true,
      children: [new TableCell({
        width: { size: W, type: WidthType.DXA },
        margins: { top: 100, bottom: 100, left: 140, right: 140 },
        verticalAlign: VerticalAlign.CENTER,
        children: [new Paragraph({
          alignment: AlignmentType.CENTER,
          spacing: { after: 0 },
          keepNext: true,
          children: [new ImageRun({
            type: 'png',
            data: fs.readFileSync(file),
            transformation: { width: w, height: h },
            altText: { title: alt, description: alt, name: alt },
          })],
        })],
      })],
    })],
  });
  return [t, caption(key, cap)];
}

function dataTable(cols, head, rows, key, cap) {
  const total = cols.reduce((a, b) => a + b, 0);
  if (total !== W) throw new Error('column widths must sum to ' + W + ' (got ' + total + ') in ' + cap);
  const b = line('808080');
  const margins = { top: 60, bottom: 60, left: 100, right: 100 };
  const headRow = new TableRow({
    cantSplit: true,
    tableHeader: true,
    children: head.map((h, i) => new TableCell({
      width: { size: cols[i], type: WidthType.DXA },
      shading: { type: ShadingType.CLEAR, fill: 'D9D9D9', color: 'auto' },
      margins,
      children: [new Paragraph({ spacing: { after: 0 }, children: [new TextRun({ text: h, font: ARIAL, size: 18, bold: true })] })],
    })),
  });
  const body = rows.map((r, ri) => new TableRow({
    cantSplit: true,
    children: r.map((c, i) => new TableCell({
      width: { size: cols[i], type: WidthType.DXA },
      margins,
      children: String(c).split('\n').map((ln) => new Paragraph({
        spacing: { after: 0 },
        keepNext: ri === rows.length - 1,
        children: rich(ln, { size: 19, codeSize: 18 }),
      })),
    })),
  }));
  const t = new Table({
    width: { size: W, type: WidthType.DXA },
    columnWidths: cols,
    layout: TableLayoutType.FIXED,
    borders: { top: b, bottom: b, left: b, right: b, insideHorizontal: b, insideVertical: b },
    rows: [headRow, ...body],
  });
  return [t, caption(key, cap)];
}

function tocLine(text, page, level) {
  const chapter = level === 1;
  return new Paragraph({
    tabStops: [{ type: TabStopType.RIGHT, position: W, leader: LeaderType.DOT }],
    spacing: chapter ? { before: 160, after: 20 } : { before: 20, after: 20 },
    indent: { left: chapter ? 0 : 400 },
    children: [
      new TextRun({ text, font: chapter ? ARIAL : TNR, size: chapter ? 21 : 20, bold: chapter }),
      new TextRun({ text: '\t' + page, font: chapter ? ARIAL : TNR, size: chapter ? 21 : 20, bold: chapter }),
    ],
  });
}

// render a list of block descriptors into docx children
function render(blocks) {
  const out = [];
  for (const b of blocks) {
    switch (b.t) {
      case 'h1': out.push(h1(b.text, b)); break;
      case 'h2': out.push(h2(b.text)); break;
      case 'h3': out.push(h3(b.text)); break;
      case 'p': out.push(para(b.text, b)); break;
      case 'ul': out.push(...bullets(b.items)); break;
      case 'ol': out.push(...steps(b.items)); break;
      case 'code': out.push(...codeFrame(b.lines, b.fig, b.cap)); break;
      case 'table': out.push(...dataTable(b.cols, b.head, b.rows, b.fig, b.cap)); break;
      case 'image': out.push(...imageFrame(b.file, b.w, b.h, b.fig, b.cap, b.alt)); break;
      default: throw new Error('unknown block ' + b.t);
    }
  }
  return out;
}

module.exports = { assignFigures, figNumbers, resolveRefs, rich, para, arial, h1, h2, h3, tocLine, render, TNR, ARIAL, COURIER, W };
