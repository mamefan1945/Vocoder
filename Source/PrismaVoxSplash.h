#pragma once

// Vector startup artwork: the PrismaVox wordmark is part of the splash image.
inline constexpr const char* prismaVoxSplashSvg = R"PRISMAVOX(
<svg xmlns="http://www.w3.org/2000/svg" width="1380" height="790" viewBox="0 0 1380 790">
  <defs>
    <radialGradient id="bg" cx="50%" cy="39%" r="72%">
      <stop offset="0" stop-color="#101e35"/>
      <stop offset=".58" stop-color="#081222"/>
      <stop offset="1" stop-color="#040812"/>
    </radialGradient>
    <linearGradient id="spectrum" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#ff526b"/>
      <stop offset=".2" stop-color="#ffb84d"/>
      <stop offset=".4" stop-color="#d7e66b"/>
      <stop offset=".58" stop-color="#45dfb0"/>
      <stop offset=".76" stop-color="#43b9ff"/>
      <stop offset="1" stop-color="#a77aff"/>
    </linearGradient>
    <linearGradient id="glass" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#eaf8ff" stop-opacity=".88"/>
      <stop offset=".48" stop-color="#72baff" stop-opacity=".28"/>
      <stop offset="1" stop-color="#d5eaff" stop-opacity=".72"/>
    </linearGradient>
    <linearGradient id="word" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#f4f8ff"/>
      <stop offset=".7" stop-color="#dcecff"/>
      <stop offset="1" stop-color="#8bdcff"/>
    </linearGradient>
    <filter id="softGlow" x="-60%" y="-60%" width="220%" height="220%">
      <feGaussianBlur stdDeviation="13" result="blur"/>
      <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <filter id="beamGlow" x="-30%" y="-300%" width="160%" height="700%">
      <feGaussianBlur stdDeviation="7"/>
    </filter>
  </defs>

  <rect width="1380" height="790" fill="url(#bg)"/>

  <!-- Quiet technical texture -->
  <g stroke="#8ab9e8" stroke-opacity=".07" stroke-width="1">
    <path d="M130 170H1250M130 590H1250"/>
    <path d="M230 120V630M1150 120V630"/>
    <circle cx="690" cy="300" r="210" fill="none"/>
    <circle cx="690" cy="300" r="250" fill="none" stroke-dasharray="2 12"/>
  </g>

  <!-- Incoming beam and refracted spectrum -->
  <path d="M250 278H550" stroke="#dcecff" stroke-width="5" opacity=".55" filter="url(#beamGlow)"/>
  <path d="M250 278H550" stroke="#f2f8ff" stroke-width="2.5" opacity=".9"/>
  <path d="M825 278L1135 158M825 278L1165 218M825 278L1190 278M825 278L1165 338M825 278L1135 398"
        fill="none" stroke="url(#spectrum)" stroke-width="3" opacity=".78" filter="url(#softGlow)"/>

  <!-- Glass prism -->
  <path d="M690 105L525 390H855Z" fill="#183453" fill-opacity=".42"
        stroke="url(#glass)" stroke-width="4" filter="url(#softGlow)"/>
  <path d="M690 105V390H525Z" fill="#70c5ff" fill-opacity=".13"/>
  <path d="M690 105L855 390H690Z" fill="#ffffff" fill-opacity=".09"/>
  <path d="M690 105L690 390" stroke="#e8f7ff" stroke-opacity=".48" stroke-width="2"/>
  <path d="M525 390H855" stroke="#9ed5ff" stroke-opacity=".55" stroke-width="2"/>

  <!-- Subtle waveform integrated with the spectrum -->
  <path d="M260 468h170l16 -13 15 26 17 -43 19 61 20 -78 21 91 19 -57 17 29 17 -16h98
           m-98 0h98l16 -13 15 26 17 -43 19 61 20 -78 21 91 19 -57 17 29 17 -16h98"
        fill="none" stroke="url(#spectrum)" stroke-width="2.5" stroke-linecap="round"
        stroke-linejoin="round" opacity=".58"/>

  <!-- Wordmark is embedded in the artwork itself -->
  <text x="690" y="565" text-anchor="middle" fill="url(#word)"
        font-family="Segoe UI, Arial, sans-serif" font-size="76" font-weight="600"
        letter-spacing="3">PrismaVox</text>
  <path d="M555 590H825" stroke="url(#spectrum)" stroke-width="2" opacity=".7"/>
  <circle cx="540" cy="590" r="3" fill="#56c7ff" opacity=".8"/>
  <circle cx="840" cy="590" r="3" fill="#9b7bff" opacity=".8"/>
</svg>
)PRISMAVOX";
