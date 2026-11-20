#pragma once

// Clean, compact VoiceForge mark: waveform above the product name.
inline constexpr const char* voiceForgeLogoSvg = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="900" height="300" viewBox="0 0 900 300">
 <g fill="none" stroke-linecap="round" stroke-linejoin="round">
  <path d="M258 74 H305 L324 48 L345 100 L367 30 L389 112 L411 56 L431 74 H469"
        stroke="#6fa9d2" stroke-width="7"/>
  <path d="M431 74 C455 74 455 20 480 20 C505 20 505 128 530 128 C555 128 555 43 580 43 C605 43 605 74 629 74 H642"
        stroke="#8a91c9" stroke-width="7"/>
 </g>
 <text x="450" y="204" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif"
       font-size="88" font-weight="600" letter-spacing="-2.5" fill="#e4eaf0">VoiceForge</text>
 <path d="M255 226 H645" stroke="#526779" stroke-width="2"/>
 <text x="450" y="263" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif"
       font-size="21" font-weight="500" letter-spacing="3.1" fill="#aebbc8">VOCODER  /  VOCAL HARMONIZER</text>
</svg>
)SVG";