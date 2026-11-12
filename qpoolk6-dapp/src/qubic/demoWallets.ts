// 10 wallets funded for this public demo -- each has 60,000,000 QU and 50,000
// units of a DEMO token with management rights already granted to QPoolK6
// (ready for DepositHoldingAsset with no extra setup). Shared devnet: anyone
// using this dapp can pick the same one you do, same as every other wallet
// here -- see WalletPanel's testnet warning.
export interface DemoWallet {
  label: string
  seed: string
  identity: string
}

export const DEMO_WALLETS: DemoWallet[] = [
  { label: 'Demo wallet 1 (already has an example pool + deposit)', seed: 'qcggqujoyyinludnauopfkpfunbawozwjpngqtqgaqtmnmolpsqolhy', identity: 'LULRZAISAYHMPGDFCCGUHEQYMILAUJFHKKKJFVRDUFTUEQDOTMDSAIYBJFGM' },
  { label: 'Demo wallet 2', seed: 'qahtfrfoehrweevxeqldvhgrgzrggmikhcnkhmtblimufofwvtifsdj', identity: 'TXGGBVGLJXDARAHGFKXLNWDJCEYAPYBJDCHZJQTWYDVJLMYRYBRMMFKAJMKF' },
  { label: 'Demo wallet 3', seed: 'xevbwnidvncmmyqxzrhtdjctuvmghpudmyttvcepaeviysgrtczfofw', identity: 'PXNGGPRVTJSWJGDPQKZPAXEKMNXCDMHUWUJNZQSRFDQSWMPWZZDUQAGBFSZB' },
  { label: 'Demo wallet 4', seed: 'sahaurjwiokgqbtwibeqdtwtlrmzgoegynaaetzzjhlskemjpqnreld', identity: 'CZHGLJIPIHBSEHROHSNAGBXTXEXAPKQYKUZDQJNGSEEHBKRVQPRVHFUCVLNB' },
  { label: 'Demo wallet 5', seed: 'vhcbvnyywddcfzbfknptcmthxumxqctybnzhaqzhsirxhghkdhgmtap', identity: 'VIFFQRVYJMLEEEVUBCANPFHYCIQASXQGRZLPHPTCEEJAZSTEVXTXJGCAMXTB' },
  { label: 'Demo wallet 6', seed: 'vsjnysxulnvqgdwxhebbrhsgvzakzbjfkcwpxaefwrhunnrblifipcm', identity: 'BDATQYYJFRTLTENWADLVHJSIDOPDPPMMZSTLWRYJNGSHAXCNZCPYIDTFJBGC' },
  { label: 'Demo wallet 7', seed: 'naswlkqigfyhcsqjmkxfqwtpzfiuomlfoabpbpkxyfpqrtdvzstfxjs', identity: 'DBROJBMOLUQOOFBHSKXEQOGIEEGBRGEZTPSZSDXVCDTNRADREGYMOCKBMVUI' },
  { label: 'Demo wallet 8', seed: 'svzbansvjgbnwkpwaywyufyedbhqexqbagdyjtuwxntgeaufdzggrei', identity: 'ZDRSYYYFKFPBAFWAVIJMMFEFYSEAOHLRIQROVEWIOEIRHMQVJFXRNCAEUDDG' },
  { label: 'Demo wallet 9', seed: 'gllghtyshebitizwbenxlhahzwrwuhczxszkxzwblyeueqwumxwhzcy', identity: 'TUXLZMNDSHVCMBLPKWTFQBKOZATBZVPCJVEMKLVELHOSDKMOSMMIIVVDZIND' },
  { label: 'Demo wallet 10', seed: 'tqymlxndtqhtaulkrluelmeayemaxukrtctlvqdtkpfojnfxpzbslmk', identity: 'OPUIDMMXIOGUSEZUVWLJOHCMJSXAVCCJYIGRUCLNVAIHNOHJQCSQXZJGDJPB' },
]
