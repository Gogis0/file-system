3. Domaca uloha z OS
====================


Vasou poslednou domacou ulohou bude nakodit si vlastny suborovy system takmer so
vsetkym, co k nemu patri. Tato uloha je o dost rozsiahlejsia ako predchadzajuca,
preto na ziskanie plneho poctu bodov nebude treba spravit implementaciu celeho
API.

1. Praca s diskom

Citanie/zapis na disk prebieha po sektoroch (ktore su nastavene na SECTOR_SIZE
bajtov, cize 128). Adresa sa uvadza v nasobkoch sektorov a naraz je potrebne na
disk zapisat vzdy jeden sektor (a cita sa tiez jeden sektor). Velkost disku je
tiez celociselny nasobok velkosti sektora. API je teda nasledovne:

  - hdd_size() -- vrati velkost harddisku v bajtoch (NIE v sektoroch)
  - hdd_read(sector, buffer) -- do premennej 'buffer' nacita data zo sektora
    'sector' (cisluje sa od 0)
  - hdd_write(sector, buffer) -- na disk zapise z premennej 'buffer' jeden
    sektor na adrese 'sector'

Vas suborovy system bude vediet otvarat a zatvarat subory. Na to, aby ste si
pamatali, co je otvorene, uz potrebujete nejaku pamat (idealne mimo
harddisku a pristupnu bez velkej byrokracie), ktora sa rovno bude pouzivat ako
handle (filedescriptor, popisovac, ...) toho suboru. Takuto handle si viete
vyrobit nasledovne:

  - fd_alloc() -- alokuje novu handle a vrati na nu pointer
  - fd_free(handle) -- uvolni alokovanu handle 

Handle na subor je zlozena z styroch 4bajtovych integerov (dokopy 16 bajtov),
ktore mozete pouzit, ako uznate za vhodne. Funkcie, ktore budu pracovat so
subormi, budu dostavat handle ako parameter; ulozte si don informacie, ktore
potrebujete na urcenie suboru na disku, pozicie v nom, ... .

V zdrojakoch mate opat implementovany jednoduchy a hlupy suborovy system,
ktory si v sektore 0 na disku drzi nazov a velkost jedineho suboru, ktory
moze existovat, a v sektore 1 si drzi jeho data (nevie byt teda vacsi ako
SECTOR_SIZE).

sektor 0
|-----12B------|-4B-|-----112B-----|
|meno suboru   |velk| nevyuzite    |

sektor 1
|--------------128B----------------|
| data zo suboru                   |

Vsetky cesty, ktore dostavate ako argumenty, budu zacinat lomitkom (korenovym
adresarom) ako v UNIXoch a budu absolutne -- nie je v nich adresar '.' ani '..'.

  
2. Co mate spravit?

Pozrite si subor filesystem.c; najdete v nom ukazkovu implementaciu
filesystemu (vela funkcii obsahuje len 'return FAIL'). Tieto funkcie
upravte/doplnte tak, aby sa spravali podla komentarov.

Suborov system by mal byt limitovany iba mnozstvom dostupneho miesta na disku.
To znamena, ze pouzivatel by mal vediet vyrobit (potencialne nekonecne) vela
suborov alebo adresarov. Nemali by existovat umele obmedzenia ako napr. maximalne
jeden subor bez adresarov v ukazkovej implementacii.

V praxi mozete predpokladat, ze velkost suboru bude maximalne 2^30 bajtov
(realne zrejme menej), disk bude maximalne velkosti 2 GiB (opat, realne zrejme o
dost menej). Nazov jednej polozky v ceste sa vzdy zmesti do 12 znakov a celkova
dlzka cesty neprekroci 64 znakov. Nazvy suborov su citlive na velkost pismen;
mozu obsahovat a-zA-Z0-9.-_ . Maximalny pocet poloziek v adresari mozete odhadnut
na 2^16.

Napisat FS je dost tazke. Funkcie su rozdelene na Level 1 az Level 4
(rozdelenie najdete v subore filesystem.h); v takomto poradi by malo byt rozumne
ich implementovat. Zaroven:

  - korektna implementacia L1 -- 5 bodov
  - korektna implementacia L1 + L2 -- 10 bodov
  - po korektnej implementacii L3 mate zisk 1 bonusovy bod 
  - po korektnej implementacii L4 mate zisk 1 (dalsi) bonusovy bod.

Korektna implementacia L3 znamena, ze _vsetko_ bude zvladat korektne pracovat
s adresarmi. 


3. Ako to testovat?

Kompilacia: spustenie prikazu 'make' v adresari so subormi.
Spustenie programu: ./wrapper, pripadne ./test pre testovanie.

Vsetky potrebne subory najdete v adresari c/. Pre make
je tam Makefile, pomocou ktoreho si viete spravit jednak wrapper, co je vas
filesystem obaleny wrapperom, ktory caka na prikazy. Viete si taktiez
skompilovat test, do ktoreho mozete priamo pisat prikazy, ktore ma vas
filesystem vykonavat. Momentalne je v nom jednoducha ukazka.

Vas program bude citat prikazy zo standardneho vstupu a
vypisovat vysledky na standardny vystup. Vacsinou plati konvencia, ze meno
funkcie bez 'fs_' na zaciatku na standardnom vstupe sposobi zavolanie tej
funkcie. Riadky oznacene ako I: znamenaju vas vstup, O: je vystup od programu.
Za # je komentar.

I: creat "/test.txt"	       # Poziadavka na vytvorenie noveho suboru test.txt
O: 47423            	       # Vsetko je OK, dostali sme file handle
I: write 47423 38656c6c6f 5	   # Zapise do suboru 5 bajtov, zakodovane v hexastringu
O: 5	                       # bolo zapisanych 5 bajtov
I: tell 47423	               # Na akej pozicii v subore momentalne sme?
O: 5	                       # na 5-tom bajte
I: close 47423	               # Zavri file handle
O: 0	                       # OK

Data pre funkcie read a write su kodovane do hexa retazcov -- co dvojica
znakov, to jeden bajt v hexadecimalnom zapise. Funkcia read tiez vracia nacitany
buffer v tomto formate.

Dalsia moznost na testovanie je subor test.c, v ktorom mate ukazku volania
zopar funkcii filesystemu.


4. Prakticke odpovede na (ne-)vyslovene otazky:

- cesty sa vzdy zacinaju lomitkom, a az na cestu '/' nikdy nekoncia lomitkom
- jeden subor ma najviac jeden otvoreny filehandle
- otvoreny subor sa nemoze mazat/prepisovat
- rename vie premenovat aj adresare; poznamka pri nom sa tyka vytvarania novych adresarov
- write prepisuje obsah suboru, nie doplna don nove data
- readdir vracia -1 az po tom, co korektne vrati poslednu polozku v adresari (teda, je na konci adresara)
- pri hardlinkoch je naraz otvoreny iba jeden z linkov
- symlinky mozu byt aj na adresare
- symlinky sa daju vytvorit aj na neexistujuce subory; kym ciel symlinku neexistuje, symlink sa neda otvorit
