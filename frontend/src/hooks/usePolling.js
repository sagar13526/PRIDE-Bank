import { useState, useEffect } from 'react';

export function usePolling(fetcher, interval = 1000) {
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let isMounted = true;

    async function load() {
      try {
        const result = await fetcher();
        if (isMounted) {
          setData(result);
          setError(null);
          setLoading(false);
        }
      } catch (err) {
        if (isMounted) {
          setError(err.message);
          setLoading(false);
        }
      }
    }

    load();
    const timer = setInterval(load, interval);

    return () => {
      isMounted = false;
      clearInterval(timer);
    };
  }, [fetcher, interval]);

  return { data, error, loading };
}
