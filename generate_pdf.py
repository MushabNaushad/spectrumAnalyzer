#!/usr/bin/env python3
"""
Generate a publication-quality PDF for the LTSpice Simulation & Circuit Guide
using ReportLab with professional formatting, headers/footers, and styling.
"""

import os
from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether, HRFlowable
)
from reportlab.pdfgen import canvas

PDF_PATH = "/Users/methalabeywickrama/Documents/spectrum/spectrumAnalyzer/LTSPICE_SIMULATION_GUIDE.pdf"

# Numbered canvas for "Page X of Y"
class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_page_number(num_pages)
            canvas.Canvas.showPage(self)
        canvas.Canvas.save(self)

    def draw_page_number(self, page_count):
        self.saveState()
        self.setFont("Helvetica", 8)
        self.setFillColor(colors.HexColor("#666666"))
        
        # Header (pages 2+)
        if self._pageNumber > 1:
            self.drawString(54, 11 * inch - 36, "Discrete Superheterodyne Spectrum Analyzer — LTSpice Simulation Guide")
            self.setStrokeColor(colors.HexColor("#D0D7DE"))
            self.setLineWidth(0.5)
            self.line(54, 11 * inch - 42, 8.5 * inch - 54, 11 * inch - 42)
            
        # Footer (all pages)
        self.setStrokeColor(colors.HexColor("#D0D7DE"))
        self.setLineWidth(0.5)
        self.line(54, 46, 8.5 * inch - 54, 46)
        
        page_text = f"Page {self._pageNumber} of {page_count}"
        self.drawRightString(8.5 * inch - 54, 32, page_text)
        self.drawString(54, 32, "CONFIDENTIAL & PROPRIETARY — Engineering Design Document")
        self.restoreState()


def build_pdf():
    doc = SimpleDocTemplate(
        PDF_PATH,
        pagesize=letter,
        leftMargin=54,
        rightMargin=54,
        topMargin=54,
        bottomMargin=54
    )

    styles = getSampleStyleSheet()

    # Custom Palette
    PRIMARY = colors.HexColor("#1A365D")   # Deep Navy
    SECONDARY = colors.HexColor("#2B6CB0") # Steel Blue
    ACCENT = colors.HexColor("#2C7A7B")    # Teal
    DARK = colors.HexColor("#2D3748")      # Charcoal Body Text
    BG_BOX = colors.HexColor("#F7FAFC")    # Cool Light Gray Box
    BORDER = colors.HexColor("#CBD5E0")    # Border Gray
    CODE_BG = colors.HexColor("#EDF2F7")   # Code block background

    title_style = ParagraphStyle(
        'DocTitle',
        parent=styles['Heading1'],
        fontName='Helvetica-Bold',
        fontSize=20,
        leading=24,
        textColor=PRIMARY,
        spaceAfter=4
    )

    subtitle_style = ParagraphStyle(
        'DocSubtitle',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=11,
        leading=15,
        textColor=SECONDARY,
        spaceAfter=12
    )

    h1_style = ParagraphStyle(
        'SectionH1',
        parent=styles['Heading2'],
        fontName='Helvetica-Bold',
        fontSize=13,
        leading=17,
        textColor=PRIMARY,
        spaceBefore=14,
        spaceAfter=6,
        keepWithNext=True
    )

    h2_style = ParagraphStyle(
        'SectionH2',
        parent=styles['Heading3'],
        fontName='Helvetica-Bold',
        fontSize=10.5,
        leading=14,
        textColor=SECONDARY,
        spaceBefore=10,
        spaceAfter=4,
        keepWithNext=True
    )

    body_style = ParagraphStyle(
        'BodyDark',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=9,
        leading=12.5,
        textColor=DARK,
        spaceAfter=4
    )

    bullet_style = ParagraphStyle(
        'BulletText',
        parent=body_style,
        leftIndent=12,
        firstLineIndent=-8,
        spaceAfter=3
    )

    code_style = ParagraphStyle(
        'CodeBlock',
        parent=styles['Normal'],
        fontName='Courier',
        fontSize=7.5,
        leading=9.5,
        textColor=colors.HexColor("#1A202C")
    )

    story = []

    # Title Banner
    story.append(Paragraph("Discrete Superheterodyne Spectrum Analyzer", title_style))
    story.append(Paragraph("Comprehensive Circuit Implementation & LTSpice Simulation Guide", subtitle_style))
    story.append(HRFlowable(width="100%", thickness=1.5, color=PRIMARY, spaceAfter=10))

    # Overview Callout Box
    overview_data = [[
        Paragraph(
            "<b>Architecture:</b> Swept Superheterodyne &nbsp;|&nbsp; <b>RF Range:</b> 0 to 30 kHz (Audio-to-Ultrasound)<br/>"
            "<b>IF Channels:</b> 25 kHz (Ch 2) & 35 kHz (Ch 3) &nbsp;|&nbsp; <b>Resolution Bandwidth (RBW):</b> 200 Hz (Q = 125 to 175)<br/>"
            "<b>Active Parts:</b> TL072 JFET Op-Amps (10 ICs), 2N2222 / 2N3904 NPN BJTs, 1N4148 Diodes, ADS1115 ADC",
            body_style
        )
    ]]
    overview_table = Table(overview_data, colWidths=[504])
    overview_table.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), BG_BOX),
        ('BOX', (0,0), (-1,-1), 1, SECONDARY),
        ('TOPPADDING', (0,0), (-1,-1), 6),
        ('BOTTOMPADDING', (0,0), (-1,-1), 6),
        ('LEFTPADDING', (0,0), (-1,-1), 10),
        ('RIGHTPADDING', (0,0), (-1,-1), 10),
    ]))
    story.append(overview_table)
    story.append(Spacer(1, 10))

    # Section 1: System Signal Chain
    story.append(Paragraph("1. System Signal Chain Architecture", h1_style))
    story.append(Paragraph(
        "The instrument follows the classic swept superheterodyne architecture. An incoming RF signal is buffered, "
        "mixed down to a fixed intermediate frequency (IF) by a swept local oscillator (LO), filtered through high-Q "
        "bandpass stages, envelope detected, smoothed via a video bandwidth (VBW) filter, and displayed against the LO sweep ramp (X-axis):",
        body_style
    ))

    chain_box = [[
        Paragraph(
            "<b>[ RF Input ]</b>  --&gt;  <b>[ Preamp / Attenuator ]</b>  --&gt;  <b>[ 2N2222 Gilbert Mixer ]</b>  --&gt;  <b>[ Active IF Filter Bank ]</b><br/>"
            "                                                                                  |<br/>"
            "<b>[ X-OUT (Tuning Ramp) ]</b> &lt;-- <b>[ Sawtooth VCO (LO) ]</b> -------------------------------┘<br/>"
            "                                                                                  |<br/>"
            "                                                                                  v<br/>"
            "<b>[ Y-OUT (Spectrum) ]</b>    &lt;-- <b>[ Video BW Filter ]</b>    &lt;-- <b>[ Envelope Detector ]</b>",
            code_style
        )
    ]]
    chain_table = Table(chain_box, colWidths=[504])
    chain_table.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), CODE_BG),
        ('BOX', (0,0), (-1,-1), 0.5, BORDER),
        ('TOPPADDING', (0,0), (-1,-1), 6),
        ('BOTTOMPADDING', (0,0), (-1,-1), 6),
        ('LEFTPADDING', (0,0), (-1,-1), 8),
        ('RIGHTPADDING', (0,0), (-1,-1), 8),
    ]))
    story.append(chain_table)
    story.append(Spacer(1, 10))

    # Section 2: Circuit Specifications
    story.append(Paragraph("2. Block-by-Block Circuit Specifications", h1_style))

    story.append(Paragraph("Block 1: RF Input Preamp & Attenuator (<code>01_RF_Frontend.asc</code>)", h2_style))
    story.append(Paragraph("• <b>Function:</b> High input impedance buffer (1 MΩ) with selectable gain (1x, 10x / +20 dB) to condition weak audio signals (10 mV to 1 Vrms) to optimal mixer level (0.5 to 1.0 Vpp).", bullet_style))
    story.append(Paragraph("• <b>Components:</b> TL072 JFET op-amp (±9V to ±12V rails), Cin = 1.0 µF, Rin = 100 Ω, Rbias = 1.0 MΩ, Rf = 9.0 kΩ, Rg = 1.0 kΩ, Cout = 1.0 µF.", bullet_style))

    story.append(Paragraph("Block 2: Discrete Gilbert Cell Four-Quadrant Mixer (<code>02_Gilbert_Mixer.asc</code>)", h2_style))
    story.append(Paragraph("• <b>Topology:</b> 6-transistor active Gilbert cell using 2N2222 BJTs. Driven by single-ended RF and differential LO.", bullet_style))
    story.append(Paragraph("• <b>Bias Network (+12V rail):</b> R1=3.3k, R2=2.2k, R4=2.2k, R3=1.5k; Base resistors R8..R11, R6 = 10 kΩ; Tail resistor R5 = 390 Ω; Collector loads R12=R7=2.2 kΩ.", bullet_style))
    story.append(Paragraph("• <b>Coupling Capacitors:</b> Bypass C1, C2, C3 = 10 µF electrolytic; LO coupling C4, C5 = 100 nF; Output Cout = 1.0 µF.", bullet_style))

    story.append(Paragraph("Block 3: IF Active Bandpass Filter Bank (<code>03_IF_Filter_Bank.asc</code>)", h2_style))
    story.append(Paragraph("• <b>Channel 2 (25 kHz Sallen-Key):</b> f0 = 25,000 Hz, BW = 200 Hz (Q = 125). TL072 op-amps, R3=R5=796 kΩ (1%), R4=6.34 kΩ (1%), C3=C4=1.0 nF (C0G). Includes input divider (10k / 2.2k, -15 dB) to prevent passband clipping.", bullet_style))
    story.append(Paragraph("• <b>Channel 3 (35 kHz MFB Filter Redesign):</b> f0 = 35,000 Hz, BW = 200 Hz (Q = 175, Av = 2.0). Replaced oscillating Sallen-Key with Multiple Feedback (MFB) topology. Components: C1=C2=1.0 nF, R3 (feedback) = 1.591 MΩ, R1 (input) = 397.8 kΩ, R2 (ground) = 12.98 Ω.", bullet_style))

    story.append(Paragraph("Block 4: Diode Envelope Detector & Buffer (<code>04_Envelope_Detector.asc</code>)", h2_style))
    story.append(Paragraph("• <b>Components:</b> 1N4148 fast rectifier diode, C = 10 nF reservoir cap, R = 22 kΩ discharge resistor (tau = 220 µs). TL072 unity-gain buffer.", bullet_style))

    story.append(Paragraph("Block 5: Video Bandwidth (VBW) Low-Pass Filter (<code>05_VBW_Filter.asc</code>)", h2_style))
    story.append(Paragraph("• <b>Selectable Modes:</b> Low (fc = 50 Hz, R = 33k, C = 100n), Medium (fc = 200 Hz, R = 10k, C = 82n), High (fc = 1 kHz, R = 10k, C = 15n).", bullet_style))

    story.append(Paragraph("Block 6: Sawtooth Sweep Generator & VCO (<code>06_Sawtooth_VCO.asc</code>)", h2_style))
    story.append(Paragraph("• <b>Topology:</b> TL072 Integrator (Ctime = 10n..100n, Rcharge = 100k, Icharge = 50 µA) + TL072 Schmitt Trigger + 2N3904 Reset BJT. Produces a 0V to 5V linear ramp tuning LO from 20 kHz to 50 kHz.", bullet_style))

    story.append(Spacer(1, 10))

    # Section 3: The 6 Simulations
    story.append(Paragraph("3. Comprehensive Simulation Suite (Implementation & Directives)", h1_style))

    simulations = [
        ("Sim 1: AC Frequency Response & Stability Analysis",
         ".ac dec 200 10k 50k\n.meas AC f_center MAX mag(V(IF_out_2))\n.meas AC f_low WHEN mag(V(IF_out_2))=f_center/sqrt(2) FALL=1\n.meas AC f_high WHEN mag(V(IF_out_2))=f_center/sqrt(2) RISE=1\n.meas AC bw PARAM (f_high - f_low)",
         "Validates resonant frequency f0 = 25 kHz ± 100 Hz, BW = 200 Hz ± 25 Hz, and verifies unconditional stability (monotonic phase decrease)."),

        ("Sim 2: Component Tolerance & Monte Carlo Analysis",
         ".param R3_val = {mc(796k, 0.01)}\n.param R4_val = {mc(6.34k, 0.01)}\n.param C3_val = {mc(1n, 0.05)}\n.param C4_val = {mc(1n, 0.05)}\n.step param run 1 100 1",
         "100-run statistical sweep ensuring >95% of runs remain within 25 kHz ± 250 Hz (1.0%) center frequency spread with 1% resistors and 5% caps."),

        ("Sim 3: Worst-Case Sensitivity Analysis",
         ".param R3_wc = {wc(796k, 0.01, 1)}\n.param R5_wc = {wc(796k, 0.01, 2)}\n.param C3_wc = {wc(1n, 0.05, 3)}\n.param C4_wc = {wc(1n, 0.05, 4)}\n.step param run 1 16 1",
         "Evaluates maximum frequency error bounding box (delta_f_max) across all binary tolerance corner permutations."),

        ("Sim 4: Thermal Drift Analysis",
         ".model RES_PRECISION R (tc1=50e-6)  ; 50 ppm/°C metal film\n.step temp 0 70 10",
         "Quantifies frequency shift across temperature caused by semiconductor Vbe tempco (-2 mV/°C), op-amp offset drift, and resistor TCR (< 50 Hz drift)."),

        ("Sim 5: Mixer Intermodulation Distortion & Linearity (FFT)",
         "* Two-Tone Input: 5.0 kHz + 5.2 kHz (100 mVpk each); LO = 20.0 kHz\n.tran 0 20m 10m 100n",
         "FFT on V(mix_out). Measures mixing products (25.0, 25.2 kHz) and 3rd-order intermod products (24.8, 25.4 kHz). Verifies SFDR >= 45 dBc."),

        ("Sim 6: Master Dynamic Multi-Tone Sweep",
         "* Three-tone RF: 4.0 kHz + 7.0 kHz + 12.0 kHz; Sweeping LO: 20 kHz to 40 kHz\n.tran 0 100m 0 1u",
         "Plots V(env_out) vs V(ramp) in simulated X-Y display mode. Confirms 3 distinct, symmetric spectral peaks without asymmetrical ringing tails.")
    ]

    for title, directive, desc in simulations:
        story.append(Paragraph(title, h2_style))
        story.append(Paragraph(f"<b>Goal & Description:</b> {desc}", body_style))
        
        code_box = [[Paragraph(directive.replace('\n', '<br/>'), code_style)]]
        tbl = Table(code_box, colWidths=[504])
        tbl.setStyle(TableStyle([
            ('BACKGROUND', (0,0), (-1,-1), CODE_BG),
            ('BOX', (0,0), (-1,-1), 0.5, BORDER),
            ('TOPPADDING', (0,0), (-1,-1), 4),
            ('BOTTOMPADDING', (0,0), (-1,-1), 4),
            ('LEFTPADDING', (0,0), (-1,-1), 6),
            ('RIGHTPADDING', (0,0), (-1,-1), 6),
        ]))
        story.append(tbl)
        story.append(Spacer(1, 6))

    # Section 4: Summary Table of Deliverables
    story.append(Spacer(1, 6))
    story.append(Paragraph("4. Summary of Simulation Deliverables", h1_style))

    table_data = [
        [Paragraph("<b>#</b>", body_style), Paragraph("<b>Schematic File</b>", body_style), Paragraph("<b>Simulation Type</b>", body_style), Paragraph("<b>Key Directive</b>", body_style), Paragraph("<b>Target Deliverable</b>", body_style)],
        [Paragraph("1", body_style), Paragraph("03_IF_Filter_Bank.asc", body_style), Paragraph("AC Response", body_style), Paragraph("<code>.ac dec 200 10k 50k</code>", body_style), Paragraph("f0 = 25 kHz, BW = 200 Hz, Q = 125, Stable", body_style)],
        [Paragraph("2", body_style), Paragraph("03_IF_Filter_Bank.asc", body_style), Paragraph("Monte Carlo", body_style), Paragraph("<code>.step param run 1 100 1</code>", body_style), Paragraph("Δf0 < ±250 Hz across 100 runs (1% R)", body_style)],
        [Paragraph("3", body_style), Paragraph("03_IF_Filter_Bank.asc", body_style), Paragraph("Temperature", body_style), Paragraph("<code>.step temp 0 70 10</code>", body_style), Paragraph("Δf0 < 50 Hz over ΔT = 40 °C", body_style)],
        [Paragraph("4", body_style), Paragraph("02_Gilbert_Mixer.asc", body_style), Paragraph("Linearity / FFT", body_style), Paragraph("<code>.tran 0 20m 10m 100n</code>", body_style), Paragraph("SFDR ≥ 45 dBc, conversion gain", body_style)],
        [Paragraph("5", body_style), Paragraph("06_Sawtooth_VCO.asc", body_style), Paragraph("Ramp Linearity", body_style), Paragraph("<code>.tran 0 50m 0 1u</code>", body_style), Paragraph("0V to 5V linear ramp, flyback < 0.5 ms", body_style)],
        [Paragraph("6", body_style), Paragraph("SpectrumAnalyzer_Master.asc", body_style), Paragraph("Master Sweep", body_style), Paragraph("<code>.tran 0 100m 0 1u</code>", body_style), Paragraph("Full 3-tone spectrum in X-Y display mode", body_style)]
    ]

    summary_table = Table(table_data, colWidths=[20, 120, 90, 130, 144])
    summary_table.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), SECONDARY),
        ('TEXTCOLOR', (0,0), (-1,0), colors.white),
        ('BOTTOMPADDING', (0,0), (-1,0), 5),
        ('TOPPADDING', (0,0), (-1,0), 5),
        ('GRID', (0,0), (-1,-1), 0.5, BORDER),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, BG_BOX]),
        ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
        ('TOPPADDING', (0,1), (-1,-1), 4),
        ('BOTTOMPADDING', (0,1), (-1,-1), 4),
    ]))
    story.append(summary_table)

    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"PDF successfully regenerated at: {PDF_PATH}")

if __name__ == "__main__":
    build_pdf()
