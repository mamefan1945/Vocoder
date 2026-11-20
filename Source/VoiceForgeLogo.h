#pragma once

// Simple vector wordmark for the VoiceForge startup splash.
inline constexpr const char* voiceForgeLogoSvg = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="900" height="250" viewBox="0 0 900 250">
 <g fill="none" stroke-linecap="round" stroke-linejoin="round">
  <path d="M28 124 H54 L68 91 L88 158 L108 108 L125 124 H148"
        stroke="#75a9c8" stroke-width="5"/>
  <path d="M148 124 C165 124 165 69 184 69 C203 69 203 179 222 179 C241 179 241 101 260 101 C279 101 279 124 296 124"
        stroke="#75a9c8" stroke-width="5"/>
  <path d="M296 124 H320" stroke="#75a9c8" stroke-width="5"/>
 </g>
 <text x="350" y="139" font-family="Segoe UI, Arial, sans-serif" font-size="76"
       font-weight="600" letter-spacing="-2" fill="#e4eaf0">VoiceForge</text>
 <path d="M352 160 H810" stroke="#526779" stroke-width="2"/>
 <text x="354" y="198" font-family="Segoe UI, Arial, sans-serif" font-size="22"
       font-weight="500" letter-spacing="3.2" fill="#aebbc8">VOCODER  /  VOCAL HARMONIZER</text>
</svg>
)SVG";
