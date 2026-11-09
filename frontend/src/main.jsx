import React from 'react';
import { createRoot } from 'react-dom/client';
import { HashRouter } from 'react-router-dom';
import App from './App.jsx';
import './styles.css';

// HashRouter: pages live at /#/operate etc., so the built files also work from any static
// server or straight from disk, without server-side route rewriting.
createRoot(document.getElementById('root')).render(
  <HashRouter>
    <App />
  </HashRouter>,
);
