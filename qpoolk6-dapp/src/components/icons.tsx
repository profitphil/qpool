// Small stroke-based icon set, consistent 20x20 viewBox -- no icon library
// dependency, just enough shapes to give every nav item and card a distinct
// visual anchor instead of plain text labels.
import type { ReactNode } from 'react'

function Icon({ children }: { children: ReactNode }) {
  return (
    <svg width="15" height="15" viewBox="0 0 20 20" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
      {children}
    </svg>
  )
}

export const IconPool = () => (
  <Icon>
    <circle cx="10" cy="6" r="3.2" />
    <path d="M4 17c0-2.8 2.7-5 6-5s6 2.2 6 5" />
  </Icon>
)
export const IconMarket = () => (
  <Icon>
    <path d="M3 7l1.3-3.5h11.4L17 7" />
    <path d="M3 7h14v8.5a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z" />
    <path d="M7.5 7v2a2.5 2.5 0 0 0 5 0V7" />
  </Icon>
)
export const IconTools = () => (
  <Icon>
    <path d="M12 5.5 14.5 3 17 5.5 14.5 8z" />
    <path d="M12.8 7.2 4.5 15.5a1.6 1.6 0 1 0 2 2L14.8 9.2" />
  </Icon>
)
export const IconStats = () => (
  <Icon>
    <path d="M4 16V9M10 16V4M16 16v-6" />
    <path d="M3 17h14" />
  </Icon>
)
export const IconTicket = () => (
  <Icon>
    <path d="M2.5 8a2.3 2.3 0 0 1 0 4v2.3a1 1 0 0 0 1 1h13a1 1 0 0 0 1-1V12a2.3 2.3 0 0 1 0-4V5.7a1 1 0 0 0-1-1h-13a1 1 0 0 0-1 1z" />
    <path d="M11 4.5v1.2M11 14.3v1.2M11 9v2" />
  </Icon>
)
export const IconBook = () => (
  <Icon>
    <path d="M3.5 16V5a2 2 0 0 1 2-2H16v13H5.5A2 2 0 0 0 3.5 16" />
    <path d="M3.5 16a2 2 0 0 0 2 2H16v-2" />
  </Icon>
)
export const IconSeat = () => (
  <Icon>
    <path d="M4.5 9.5V6a1.6 1.6 0 0 1 1.6-1.6h8a1.6 1.6 0 0 1 1.6 1.6v3.5" />
    <path d="M2.5 9.5h15V13a1.6 1.6 0 0 1-1.6 1.6H4.1A1.6 1.6 0 0 1 2.5 13z" />
    <path d="M4.5 14.6v2M15.5 14.6v2" />
  </Icon>
)
export const IconCoins = () => (
  <Icon>
    <ellipse cx="7.5" cy="5" rx="5" ry="2.5" />
    <path d="M2.5 5v5c0 1.4 2.2 2.5 5 2.5s5-1.1 5-2.5V5" />
    <path d="M2.5 10v3.5c0 1.4 2.2 2.5 5 2.5 1.7 0 3.2-.5 4.1-1.2" />
    <path d="M12.5 7.2c2.5.3 4 1.3 4 2.4 0 1.4-2.2 2.5-5 2.5l-.8-.03M12.5 12.4c2.3 0 4-1.1 4-2.5v5c0 1.4-1.9 2.4-4.2 2.4-2.3 0-4.2-1.1-4.2-2.4v-1.7" />
  </Icon>
)
export const IconWallet = () => (
  <Icon>
    <path d="M2.5 6.5A1.5 1.5 0 0 1 4 5h11a1.5 1.5 0 0 1 1.5 1.5v8A1.5 1.5 0 0 1 15 16H4a1.5 1.5 0 0 1-1.5-1.5z" />
    <path d="M12.5 10.5h3v2h-3a1 1 0 0 1 0-2z" />
    <path d="M4 5 13 5" />
  </Icon>
)
export const IconPlus = () => (
  <Icon>
    <circle cx="10" cy="10" r="7.2" />
    <path d="M10 6.8v6.4M6.8 10h6.4" />
  </Icon>
)
export const IconUpload = () => (
  <Icon>
    <path d="M10 13V4.5M6.8 7.3 10 4l3.2 3.3" />
    <path d="M3.5 13v2a1.5 1.5 0 0 0 1.5 1.5h10a1.5 1.5 0 0 0 1.5-1.5v-2" />
  </Icon>
)
export const IconVote = () => (
  <Icon>
    <path d="M10 2.5 3 5.8v1.4h14V5.8z" />
    <path d="M4.5 7.2v6M8 7.2v6M12 7.2v6M15.5 7.2v6" />
    <path d="M3 16h14" />
  </Icon>
)
export const IconLayers = () => (
  <Icon>
    <path d="M10 2.5 3 6l7 3.5L17 6z" />
    <path d="M3 10l7 3.5L17 10M3 14l7 3.5L17 14" />
  </Icon>
)
export const IconList = () => (
  <Icon>
    <path d="M7 5.5h9.5M7 10h9.5M7 14.5h9.5" />
    <circle cx="3.3" cy="5.5" r="1" fill="currentColor" stroke="none" />
    <circle cx="3.3" cy="10" r="1" fill="currentColor" stroke="none" />
    <circle cx="3.3" cy="14.5" r="1" fill="currentColor" stroke="none" />
  </Icon>
)
export const IconSearch = () => (
  <Icon>
    <circle cx="9" cy="9" r="5.5" />
    <path d="m17 17-3-3" />
  </Icon>
)
export const IconGift = () => (
  <Icon>
    <path d="M3.5 8.5h13V16a1 1 0 0 1-1 1H4.5a1 1 0 0 1-1-1z" />
    <path d="M2.5 6a1 1 0 0 1 1-1h13a1 1 0 0 1 1 1v2.5h-15z" />
    <path d="M10 5v12" />
    <path d="M10 5C8.5 5 7 4.3 7 3.1S8 1.5 9 2.3C9.7 2.8 10 3.8 10 5M10 5c1.5 0 3-.7 3-1.9S12 1.5 11 2.3C10.3 2.8 10 3.8 10 5" />
  </Icon>
)
export const IconUnlock = () => (
  <Icon>
    <rect x="4" y="9" width="12" height="8" rx="1.6" />
    <path d="M6.5 9V6.5a3.5 3.5 0 0 1 6.6-1.6" />
  </Icon>
)
export const IconChart = () => (
  <Icon>
    <path d="M3 3v12.5A1.5 1.5 0 0 0 4.5 17H17" />
    <path d="M6 13.5 9.5 9l2.5 2.5 4-5.5" />
  </Icon>
)
export const IconToken = () => (
  <Icon>
    <circle cx="10" cy="10" r="7.2" />
    <path d="M7.3 8.3h5.4M7.3 11.7h5.4M9 6.5l-1 7M12 6.5l-1 7" />
  </Icon>
)
