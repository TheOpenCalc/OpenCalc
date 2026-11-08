#ifndef PYTHON_H
#define PYTHON_H

int python_app(void);

/* Persistance des fichiers Python dans la flash interne. */
void python_files_save(void);
void python_files_load(void);
void python_files_erase(void);

#endif
