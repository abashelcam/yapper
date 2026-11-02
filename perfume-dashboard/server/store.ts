import { randomBytes } from 'node:crypto';
import { mkdir, open, readFile, rename, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { normalizeCollection } from '../shared/normalize';
import type { Perfume } from '../shared/types';
import { SEED_PERFUMES } from './seed';

export const newId = () => randomBytes(6).toString('hex');

/** Something wrong with perfumes.json itself, reported back so the user can fix the file. */
export class StoreError extends Error {}

export function createStore(dataDir: string) {
  const file = join(dataDir, 'perfumes.json');
  let queue: Promise<unknown> = Promise.resolve();

  /** Write to a temp file in the same directory, fsync, then rename over the original. */
  async function writeAtomic(perfumes: Perfume[]): Promise<void> {
    await mkdir(dataDir, { recursive: true });
    const tmp = `${file}.${process.pid}.${newId()}.tmp`;
    try {
      const fh = await open(tmp, 'w', 0o644);
      try {
        await fh.writeFile(`${JSON.stringify(perfumes, null, 2)}\n`, 'utf8');
        await fh.sync();
      } finally {
        await fh.close();
      }
      await rename(tmp, file);
    } catch (err) {
      await rm(tmp, { force: true });
      throw err;
    }
  }

  return {
    file,

    /** Creates perfumes.json with example perfumes if it doesn't exist yet. */
    async seedIfMissing(): Promise<boolean> {
      try {
        await readFile(file);
        return false;
      } catch (err) {
        if ((err as NodeJS.ErrnoException).code !== 'ENOENT') throw err;
      }
      await writeAtomic(SEED_PERFUMES);
      return true;
    },

    /** Re-reads the file every time, so hand edits show up without a restart. */
    async read(): Promise<Perfume[]> {
      let text: string;
      try {
        text = await readFile(file, 'utf8');
      } catch (err) {
        if ((err as NodeJS.ErrnoException).code === 'ENOENT') return [];
        throw err;
      }
      let data: unknown;
      try {
        data = JSON.parse(text);
      } catch (err) {
        throw new StoreError(`${file} is not valid JSON (${(err as Error).message}). Fix the file and reload.`);
      }
      try {
        return normalizeCollection(data, newId);
      } catch (err) {
        throw new StoreError(`${file}: ${(err as Error).message}`);
      }
    },

    /** Writes are queued so two quick saves can't interleave. */
    write(perfumes: Perfume[]): Promise<void> {
      const run = queue.then(() => writeAtomic(perfumes));
      queue = run.catch(() => {});
      return run;
    },

    /** Resolves once every queued write has landed. */
    flush(): Promise<unknown> {
      return queue;
    },
  };
}

export type Store = ReturnType<typeof createStore>;
