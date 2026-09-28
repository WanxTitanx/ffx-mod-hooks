# MOD-008 acquisition rules v1

Jarvis-HOOK. Stable card IDs are idempotent award receipts in the Arcana extension. This is the selected first implementation, superseding the generic acquisition proposals for this runtime candidate. Native progression is read only; no progress flag is written.

Requirements are reconciled after a verified native load and when the native Equip controller ticks or the owning thread saves. Existing saves receive already-earned cards. The Development option grants all 78 once; switching it off never removes cards.

| ID | Card | Requirement |
|---|---|---|
| 000 | 0 - The Fool - Tidus | Begin the pilgrimage |
| 001 | I - The Magician - Lulu | Obtain Bahamut |
| 002 | II - The High Priestess - Yuna | Obtain Shiva |
| 003 | III - The Empress - Moonflow | Obtain Bahamut |
| 004 | IV - The Emperor - Bevelle | Obtain the Celestial Mirror |
| 005 | V - The Hierophant - Yevon | Obtain Yojimbo |
| 006 | VI - The Lovers - Macalania Spring | Obtain Bahamut |
| 007 | VII - The Chariot - Calm Lands | Obtain Anima |
| 008 | VIII - Strength - Ifrit | Obtain all three Magus Sisters |
| 009 | IX - The Hermit - Auron | Collect all 26 Al Bhed primers |
| 010 | X - Wheel of Fortune - Al Bhed | Obtain the Celestial Mirror |
| 011 | XI - Justice - Bevelle Guardians | Obtain Yojimbo |
| 012 | XII - The Hanged Man - Fayth | Obtain Anima |
| 013 | XIII - Death - Farplane | Obtain all three Magus Sisters |
| 014 | XIV - Temperance - Moonflow | Collect all 26 Al Bhed primers |
| 015 | XV - The Devil - Anima | Defeat Dark Anima |
| 016 | XVI - The Tower - Bevelle Temple | Defeat Dark Bahamut |
| 017 | XVII - The Star - Macalania Lake | Obtain the Celestial Mirror |
| 018 | XVIII - The Moon - Macalania Woods | Obtain Yojimbo |
| 019 | XIX - The Sun - Chocobo | Dodge 200 consecutive lightning bolts |
| 020 | XX - Judgement - Sending | Defeat all eight Dark Aeon encounters |
| 021 | XXI - The World - Spira | Collect the other 77 cards |
| 022 | I - Ace of Wands - Kilika | Obtain Valefor |
| 023 | II - Two of Wands - Macalania | Obtain Ifrit |
| 024 | III - Three of Wands - Djose | Obtain Ifrit |
| 025 | IV - Four of Wands - Besaid | Obtain Ifrit |
| 026 | V - Five of Wands - Kilika Temple | Obtain Ifrit |
| 027 | VI - Six of Wands - Pilgrimage | Obtain Ifrit |
| 028 | VII - Seven of Wands - Gagazet | Obtain Ifrit |
| 029 | VIII - Eight of Wands - Thunder Plains | Obtain Bahamut |
| 030 | IX - Nine of Wands - Zanarkand Ruins | Obtain Bahamut |
| 031 | X - Ten of Wands - Zanarkand | Obtain Bahamut |
| 032 | Page of Wands - Bikanel | Obtain Ixion |
| 033 | Knight of Wands - Chocobo | Obtain Bahamut |
| 034 | Queen of Wands - Besaid Flora | Obtain Bahamut |
| 035 | King of Wands - Ifrit | Obtain Anima |
| 036 | I - Ace of Cups - Besaid Spring | Obtain Valefor |
| 037 | II - Two of Cups - Moonflow | Obtain Ixion |
| 038 | III - Three of Cups - Luca | Obtain Ixion |
| 039 | IV - Four of Cups - Kilika Woods | Obtain Ixion |
| 040 | V - Five of Cups - Moonflow Crossing | Obtain Ixion |
| 041 | VI - Six of Cups - Besaid Temple | Obtain Ixion |
| 042 | VII - Seven of Cups - Farplane | Obtain Bahamut |
| 043 | VIII - Eight of Cups - Gagazet | Obtain Bahamut |
| 044 | IX - Nine of Cups - Luca Tavern | Obtain Bahamut |
| 045 | X - Ten of Cups - Besaid Village | Obtain Bahamut |
| 046 | Page of Cups - Temple Acolyte | Obtain Shiva |
| 047 | Knight of Cups - Shoopuf Crossing | Obtain Bahamut |
| 048 | Queen of Cups - Besaid Coast | Obtain all three Magus Sisters |
| 049 | King of Cups - Spiran Sea | Collect all 26 Al Bhed primers |
| 050 | I - Ace of Swords - Gagazet | Obtain Valefor |
| 051 | II - Two of Swords - Besaid Coast | Obtain Shiva |
| 052 | III - Three of Swords - Macalania Crystal | Obtain Bahamut |
| 053 | IV - Four of Swords - Fayth Chamber | Obtain Bahamut |
| 054 | V - Five of Swords - Al Bhed | Obtain Bahamut |
| 055 | VI - Six of Swords - Moonflow Ferry | Obtain Bahamut |
| 056 | VII - Seven of Swords - Rikku | Obtain Bahamut |
| 057 | VIII - Eight of Swords - Calm Lands | Obtain the Celestial Mirror |
| 058 | IX - Nine of Swords - Zanarkand Pilgrim | Obtain Yojimbo |
| 059 | X - Ten of Swords - Fallen Guardians | Obtain Anima |
| 060 | Page of Swords - Calm Lands | Obtain Shiva |
| 061 | Knight of Swords - Chocobo Knights | Obtain all three Magus Sisters |
| 062 | Queen of Swords - Spiran Guardians | Collect all 26 Al Bhed primers |
| 063 | King of Swords - Bevelle | Obtain the Celestial Mirror |
| 064 | I - Ace of Pentacles - Besaid | Obtain Valefor |
| 065 | II - Two of Pentacles - Al Bhed | Obtain Shiva |
| 066 | III - Three of Pentacles - Djose Temple | Obtain Shiva |
| 067 | IV - Four of Pentacles - Rin | Obtain Bahamut |
| 068 | V - Five of Pentacles - Gagazet Pilgrims | Obtain Bahamut |
| 069 | VI - Six of Pentacles - Oaka | Obtain Bahamut |
| 070 | VII - Seven of Pentacles - Besaid Grove | Obtain Bahamut |
| 071 | VIII - Eight of Pentacles - Rikku | Obtain Bahamut |
| 072 | IX - Nine of Pentacles - Luca | Obtain Yojimbo |
| 073 | X - Ten of Pentacles - Besaid Village | Obtain Bahamut |
| 074 | Page of Pentacles - Al Bhed Scholar | Obtain Shiva |
| 075 | Knight of Pentacles - Chocobo | Obtain Anima |
| 076 | Queen of Pentacles - Bikanel | Obtain all three Magus Sisters |
| 077 | King of Pentacles - Rin Travel Agency | Collect all 26 Al Bhed primers |
