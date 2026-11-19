#ifndef SLOTTED_ALOHA_REPORT_XML_H
#define SLOTTED_ALOHA_REPORT_XML_H

#define XML_DECL "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
#define SHEET_NS "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
#define REL_NS "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define PACKAGE_NS "http://schemas.openxmlformats.org/package/2006/relationships"
#define CHART_NS "http://schemas.openxmlformats.org/drawingml/2006/chart"
#define DRAWING_NS "http://schemas.openxmlformats.org/drawingml/2006/main"
#define RELATIONSHIPS XML_DECL "<Relationships xmlns=\"" PACKAGE_NS "\">"
#define SHEET_START XML_DECL "<worksheet xmlns=\"" SHEET_NS "\" xmlns:r=\"" REL_NS "\">"

static const char gacContentTypes[] = XML_DECL
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
    "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
    "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
    "<Override PartName=\"/xl/worksheets/sheet2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
    "<Override PartName=\"/xl/drawings/drawing1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>"
    "<Override PartName=\"/xl/charts/chart1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>"
    "<Override PartName=\"/xl/charts/chart2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/></Types>";

static const char gacStyles[] = XML_DECL "<styleSheet xmlns=\"" SHEET_NS "\">"
    "<numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"0.000\"/></numFmts>"
    "<fonts count=\"4\">"
    "<font><sz val=\"11\"/><color rgb=\"FF1F2D3D\"/><name val=\"맑은 고딕\"/></font>"
    "<font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"맑은 고딕\"/></font>"
    "<font><b/><sz val=\"14\"/><color rgb=\"FF17365D\"/><name val=\"맑은 고딕\"/></font>"
    "<font><sz val=\"10\"/><color rgb=\"FF63758A\"/><name val=\"맑은 고딕\"/></font></fonts>"
    "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
    "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF234E70\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
    "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border><border>"
    "<left style=\"thin\"><color rgb=\"FFBAC8D6\"/></left><right style=\"thin\"><color rgb=\"FFBAC8D6\"/></right>"
    "<top style=\"thin\"><color rgb=\"FFBAC8D6\"/></top><bottom style=\"thin\"><color rgb=\"FFBAC8D6\"/></bottom><diagonal/></border></borders>"
    "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
    "<cellXfs count=\"7\">"
    "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
    "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
    "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
    "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\"/></xf>"
    "<xf numFmtId=\"3\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyAlignment=\"1\"><alignment horizontal=\"right\" vertical=\"center\"/></xf>"
    "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyAlignment=\"1\"><alignment horizontal=\"right\" vertical=\"center\"/></xf>"
    "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
    "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";

#endif
