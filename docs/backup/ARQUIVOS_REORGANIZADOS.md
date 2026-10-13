# Resumo da reorganização

- Os avisos `INSTRUCOES_*.txt` e `LEIA-ME.txt` foram movidos para `instrucoes_antigas/` porque descrevem atualizações incrementais e podem contradizer a versão atual.
- `backend/server.c` foi movido para `server_antigo.c`; a compilação atual usa somente `backend/src/server.c`.
- `script copy.js` foi movido para `script_copy_antigo.js`; a interface atual usa `frontend/script.js`.
- A cópia inativa de Mongoose que ficava em `backend/mongoose/` foi retirada. O arquivo usado pela compilação está em `backend/third_party/mongoose/`.
- Arquivos gerados (`sge_server.exe`, logs e saídas temporárias) foram removidos de `build/`. Eles são recriados ao compilar e não devem ser incluídos no Git.
- `Tema do PIM.html` e `Tema do PIM_files/` eram uma página web salva, não faziam parte do SGE e estavam sem rastreamento no Git; por isso não foram incluídos no pacote organizado.
