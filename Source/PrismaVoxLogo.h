#pragma once

// PrismaVox Option 1: a refracting prism, spectral beam, and audio waveform.
inline constexpr const char* prismaVoxLogoSvg = R"PRISMAVOX(
<svg xmlns="http://www.w3.org/2000/svg" width="900" height="500" viewBox="0 0 900 500">
  <defs>
    <linearGradient id="beam" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.04"/>
      <stop offset=".25" stop-color="#ff435d" stop-opacity=".9"/>
      <stop offset=".43" stop-color="#ffcf45" stop-opacity=".9"/>
      <stop offset=".60" stop-color="#57e6b4" stop-opacity=".9"/>
      <stop offset=".77" stop-color="#32aaff" stop-opacity=".95"/>
      <stop offset="1" stop-color="#9d5cff" stop-opacity=".9"/>
    </linearGradient>
    <linearGradient id="prism" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset=".45" stop-color="#83caff"/>
      <stop offset="1" stop-color="#d7e7ff"/>
    </linearGradient>
    <linearGradient id="vox" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#55d8ff"/>
      <stop offset="1" stop-color="#2689ff"/>
    </linearGradient>
    <filter id="glow" x="-40%" y="-40%" width="180%" height="180%">
      <feGaussianBlur stdDeviation="8" result="blur"/>
      <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
  </defs>
  <g transform="translate(0 0)">
    <path d="M110 166 L450 166 L820 166" stroke="url(#beam)" stroke-width="5" opacity=".9" filter="url(#glow)"/>
    <path d="M450 32 L300 244 L600 244 Z" fill="#10223d" fill-opacity=".56" stroke="url(#prism)" stroke-width="5" filter="url(#glow)"/>
    <path d="M450 32 L450 244 L300 244 Z" fill="#6ebcff" fill-opacity=".22"/>
    <path d="M450 32 L600 244 L450 244 Z" fill="#ffffff" fill-opacity=".12"/>
    <path d="M450 244 L600 244 L820 166" fill="none" stroke="url(#beam)" stroke-width="2" opacity=".7"/>
    <path d="M450 244 L300 244 L110 166" fill="none" stroke="#8fcaff" stroke-width="2" opacity=".55"/>
  </g>
</svg>
)PRISMAVOX";
