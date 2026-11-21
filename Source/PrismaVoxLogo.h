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
  <text x="450" y="354" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="104" font-weight="300" letter-spacing="-4" fill="#f4f7fb">Prisma<tspan fill="url(#vox)" font-weight="600">Vox</tspan></text>
  <path d="M85 421 H245 M655 421 H815" stroke="#287dca" stroke-width="2" opacity=".55"/>
  <g transform="translate(450 421)" stroke="url(#vox)" stroke-width="5" stroke-linecap="round" filter="url(#glow)">
    <path d="M-185 0v-8 M-158 0v-18 M-131 0v-31 M-104 0v-43 M-77 0v-57 M-50 0v-72 M-23 0v-88 M0 0v-102 M23 0v-88 M50 0v-72 M77 0v-57 M104 0v-43 M131 0v-31 M158 0v-18 M185 0v-8"/>
  </g>
</svg>
)PRISMAVOX";
